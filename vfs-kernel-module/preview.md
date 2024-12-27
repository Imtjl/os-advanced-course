# Showcase of solution in action

- shell commands

| ![image](https://github.com/user-attachments/assets/3fe78c64-e4e2-416a-85af-21bfc0801132) |
| ----------------------------------------------------------------------------------------- |

- logs

| ![image](https://github.com/user-attachments/assets/a5f2c272-6f0c-41d5-9dc8-f99dfdaa5f85) |
| ----------------------------------------------------------------------------------------- |

# Virtual File System (VTFS) module in the Kernel

## 1. Краткое описание

В рамках данного проекта я реализовал простую файловую систему (**VTFS**) на
уровне ядра Linux. Основная идея — дать возможность монтировать собственную VFS
и работать с ней как с обычной директорией. При этом основная логика хранится в
структуре данных, расположенной в **inode** (и/или дополнительных структурах,
связанных с VFS).

- **Ключевые изменения**:
  - Подготовил и зарегистрировал файл `vtfs.c` как модуль ядра (kernel module).
  - Реализовал внутри него функции, необходимые для работы с VFS: `lookup`,
    `create`, `mkdir`, `rmdir`, `unlink`, `read`, `write`, `iterate` (и др.).
  - Создал собственные структуры для хранения в оперативной памяти:
    `vtfs_inode`, `vtfs_dentry`, а также глобальную `vtfs_sb` для связи с
    `super_block`.
  - В итоге получилась виртуальная ФС, которая может быть примонтирована (через
    `mount -t vtfs`) и управляется обычными командами шела `ls`, `cd`, `mkdir`,
    `touch`, `rm`, и т.д.

---

## 2. Устройство VTFS

### 2.1. Основные структуры

1. **`struct vtfs_inode`**

   - Хранит номер inode (ino), режим (mode), размер (i_size) и массив данных
     (i_data).
   - Для директорий хранит информацию о том, что это `S_IFDIR`, для **файлов** —
     `S_IFREG`.
   - `i_data[1024]` позволяет хранить содержимое (для файлов) прямо в
     оперативной памяти.

2. **`struct vtfs_dentry`**

   - Связывает **имя** (d_name) с конкретным `vtfs_inode`.
   - Запоминает `d_parent_ino` (какой inode является родительским).
   - Включена в двусвязный список `vtfs_sb.dentries` (через
     `struct list_head list`).

3. **`vtfs_sb` (глобальная структура)**
   ```c
   struct {
     struct super_block* sb;
     struct list_head dentries;
   } vtfs_sb;
   ```

- sb указывает на суперблок, зарегистрированный в ядре при монтировании.
- dentries — двусвязный список vtfs_dentry, в котором мы храним все
  файлы/директории, когда работаем в RAM-режиме

4. Ядровые структуры (уже существуют в Linux):

- `struct inode` (ядро) — метаданные файла в общей системе VFS: хранит указатели
  на операции (i_op, i_fop) и номер inode (i_ino).
- `struct dentry` (ядро) — объект, который связывает имя (в каталоге) с
  конкретным inode.
- `struct super_block` (ядро) — описывает весь «том» (в данном случае
  виртуальную ФС), указывает на корневую dentry.

## Связь между командами и соответствующими операциями

| Команда Shell                     | Что делает VFS                                     | Функция в нашем модуле                               |
| --------------------------------- | -------------------------------------------------- | ---------------------------------------------------- |
| `ls /mnt/vt`                      | Открывает каталог `/mnt/vt`, вызывает перечисление | `vtfs_iterate`                                       |
| `cd /mnt/vt`                      | Ищет директорию `/mnt/vt`, проверяет доступ        | `lookup` (для корня), дальше VFS управляет переходом |
| `touch /mnt/vt/hello.txt`         | Создаёт (или открывает) файл                       | `vtfs_create`                                        |
| `echo "data" > /mnt/vt/hello.txt` | Открывает файл на запись, пишет данные             | `vtfs_write`                                         |
| `cat /mnt/vt/hello.txt`           | Открывает файл на чтение, читает данные            | `vtfs_read`                                          |
| `mkdir /mnt/vt/newdir`            | Создаёт поддиректорию                              | `vtfs_mkdir`                                         |
| `rmdir /mnt/vt/newdir`            | Удаляет директорию                                 | `vtfs_rmdir`                                         |
| `rm /mnt/vt/hello.txt`            | Удаляет файл                                       | `vtfs_unlink`                                        |

### 4.1. Загрузка модуля и монтирование

```bash
sudo insmod vtfs.ko
```

Ядро загружает наш модуль.  
В `vtfs_init` мы регистрируем файл `vtfs_fs_type` через `register_filesystem`.

```bash
sudo mount -t vtfs <token> /mnt/vt
```

Ядро вызывает vtfs_mount → mount_nodev → vtfs_fill_super.  
`vtfs_fill_super` создаёт корневой **inode**, формирует sb->s_root =
d_make_root(inode). Инициализируем vtfs_sb.sb = sb;
INIT_LIST_HEAD(&vtfs_sb.dentries);.

### 4.2. Создание файла

```bash
touch /mnt/vt/test.txt
```

Shell вызывает системный вызов open("/mnt/vt/test.txt", O_CREAT, ...).  
VFS видит, что это «vtfs», и ищет операцию create в vtfs_inode_ops.

- В vtfs_create:

Выделяется vtfs_inode с новым номером (ino), mode = S_IFREG.  
Создаётся vtfs_dentry, записывается имя "test.txt", родитель ROOT_INODE_INO.  
Запись добавляется в список vtfs_sb.dentries. Вызывается d_add(child_dentry,
inode).

### 4.3. Запись в файл

```bash
echo "Hello" > /mnt/vt/test.txt
```

Shell вызывает write (внутри echo).  
Ядро идёт в file_operations → .write = vtfs_write.

- В vtfs_write:

Находится в vtfs_sb.dentries нужный vtfs_dentry, берётся d_inode->i_data.  
Данные копируются из user space в этот массив, обновляется i_size.

### 4.4. Чтение файла

```bash
cat /mnt/vt/test.txt
```

Shell вызывает `read` (через cat).  
Ядро вызывает vtfs_dir_ops.read → vtfs_read.

- В vtfs_read:

Ищется файл по `inode->i_ino`, находится `vtfs_inode->i_data`.  
Данные копируются из `i_data` в user space (выводятся на экран).

### 4.5. Удаление файла

```bash
rm /mnt/vt/test.txt
```

Shell вызывает `unlink("/mnt/vt/test.txt")`.  
Ядро вызывает vtfs_inode_ops.unlink.

- В vtfs_unlink:

Ищется запись в vtfs_sb.dentries, где d_name == "test.txt" и parent_ino ==
ROOT_INODE_INO. Запись удаляется из списка (list_del), освобождается (kfree).

### 4.6. Размонтирование и выгрузка модуля

```bash
sudo umount /mnt/vt
```

Ядро вызывает vtfs_kill_sb, удаляет корневую dentry.

```bash
sudo rmmod vtfs
```

Ядро вызывает vtfs_exit, мы делаем unregister_filesystem. Все структуры, которые
были в оперативке, освобождаются, и файловая система исчезает.

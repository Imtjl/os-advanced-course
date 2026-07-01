# Advanced Operating Systems

[![UserTests](https://github.com/Imtjl/os-advanced-course-solutions/actions/workflows/UserTests.yml/badge.svg)](https://github.com/Imtjl/os-advanced-course-solutions/actions/workflows/UserTests.yml)

Решения лабораторных работ продвинутого трека курса «Операционные системы» (ITMO
CSE). Работа велась на уровне ядра: учебная ОС `xv6-riscv` и загружаемый модуль
ядра Linux. Каждая лабораторная выполнена в отдельной ветке, снабжена отчётом в
описании Pull Request и автоматически проверяется в CI (сборка ядра и прогон
тестов в QEMU).

**Ключевые темы:** системные вызовы и межпроцессное взаимодействие, управление
физической памятью (buddy allocator), copy-on-write fork и ленивое выделение
страниц, реализация файловой системы как модуля ядра Linux.

## Лабораторные работы

| №       | Тема                                                                 | Ветка                                                                                 | Условие                                  |
| ------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ---------------------------------------- |
| 1       | Введение в ОС: пайпы (pipes), IPC, системные вызовы `dump` / `dump2` | [`lab-1`](https://github.com/Imtjl/os-advanced-course-solutions/tree/lab-1)           | [задание](/xv6-os/doc/lab/1.md)          |
| 2       | Аллокатор физической памяти (buddy allocator)                        | [`lab-2`](https://github.com/Imtjl/os-advanced-course-solutions/tree/lab-2)           | [задание](/xv6-os/doc/lab/2.md)          |
| 3 p.1-4 | Copy-on-write fork (CoW)                                             | [`lab-3`](https://github.com/Imtjl/os-advanced-course-solutions/tree/lab-3)           | [задание](/xv6-os/doc/lab/3.md)          |
| 3 p.5   | Ленивое выделение памяти (lazy allocation)                           | [`lab-3-lazy`](https://github.com/Imtjl/os-advanced-course-solutions/tree/lab-3-lazy) | [задание](/xv6-os/doc/lab/3.md)          |
| 4       | Файловая система - модуль ядра Linux (VFS)                           | [`vfs`](https://github.com/Imtjl/os-advanced-course-solutions/tree/vfs)               | [описание](/vfs-kernel-module/README.md) |

Подробный разбор каждого решения приведён в описании соответствующего Pull
Request.

## Дополнительные задания

### `sysinfo` — информационная утилита (к лаб. 1)

Утилита и системный вызов для вывода сведений о состоянии системы: количество
процессов и открытых файлов.

Во время работы `dump2tests`:

```console
$ dump2tests > /dev &; sysinfo
System info:
 Number of procs: 4
 Number of open files: 2
$ sysinfo
System info:
 Number of procs: 7
 Number of open files: 8
```

В состоянии простоя:

```console
$ sysinfo
System info:
 Number of procs: 3
 Number of open files: 1
```

### `MLFQ` — многоуровневый планировщик с обратной связью (к лаб. 2)

Планировщик с понижением приоритета (multilevel feedback scheduler), источник —
W. Stallings, «Operating Systems: Internals and Design Principles».

| ![MLFQ](https://github.com/user-attachments/assets/5e13890b-476a-462c-8a8d-9e3c7ea971b1) |
| ---------------------------------------------------------------------------------------- |

1. Добавлено поле `priority` в структуру процесса.
2. Понижение приоритета выполняется в `yield` (_proc.c_).
3. Выбор процесса с минимальным приоритетом реализован в `scheduler` (_proc.c_).

### `RAID0` (к лаб. 4)

#### Аппаратная реализация на x86_64

Два HDD Seagate подключены по SATA3 к материнской плате X99 (сокет LGA 2011v3).
Операции выполняются процессором Xeon E5-2630v3; на плате установлено 32 ГБ ECC
DDR4, работающей на частоте 2400 МГц.

| ![RAID hardware](https://github.com/user-attachments/assets/57bb1f4a-2043-4623-9db1-47a025b8443a) |
| ------------------------------------------------------------------------------------------------- |

#### Принцип работы

RAID 0 разбивает данные на страйпы (как правило, 64 КБ) и записывает их
поочерёдно на два диска: первый блок — на диск A, второй — на диск B и так
далее. Оба диска работают параллельно, что увеличивает пропускную способность.
Прирост достигается на записи и последовательном чтении. Избыточность
отсутствует: выход из строя любого из дисков приводит к полной потере данных.

| ![RAID0 scheme](https://github.com/user-attachments/assets/53618427-77aa-4d69-a55e-fef2c51cab24) |
| ------------------------------------------------------------------------------------------------ |

#### Программная реализация массива RAID0 на Windows 10

| ![RAID0 on Windows](https://github.com/user-attachments/assets/63b5521a-48b8-4ecb-8c26-877f5eb1985f) |
| ---------------------------------------------------------------------------------------------------- |

#### Профилирование

На операциях записи массив RAID0 показывает значительный прирост пропускной
способности по сравнению с одиночным диском (JBOD).

| JBOD (одиночный HDD)                                                                     | Массив RAID0                                                                              |
| ---------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| ![JBOD](https://github.com/user-attachments/assets/9b814864-d75f-48c5-a412-aa5b93628c69) | ![RAID0](https://github.com/user-attachments/assets/da892e34-ef42-4ea1-adfd-2005d834f788) |

# Форк учебной ОС Xv6 для ITMO CSE

> [!IMPORTANT]  
> Этот репозиторий **содержит решения** лабораторных работ. Он создан
> исключительно для ревью их качества практиком курса Операционных систем ITMO
> CSE.

## Начало работы

1. [Первый запуск ОС Xv6: Linux](/doc/setup/linux.md)

2. [Настройка IDE: VSCode](/doc/dev/vscode.md)

3. [Подготовка репозитория](/doc/setup/repo.md)

## Лабораторные работы

1. [Задание 1. Введение в Xv6](/doc/lab/1.md)

2. [Задание 2. Аллокатор](/doc/lab/2.md)

3. [Задание 3. Copy-on-write fork](/doc/lab/3.md)

## Доп. задание к лаб.1 (`lab-1` branch)

Сделать утилиту для вывода системной информации => `sysinfo`

### executing `dump2tests`:

```fish
$ dump2tests > /dev &; sysinfo
System info:
 Number of procs: 4
 Number of open files: 2
$ sysinfo
System info:
 Number of procs: 7
 Number of open files: 8
```

### idle

```fish
$ sysinfo
System info:
 Number of procs: 3
 Number of open files: 1
```

## Доп. задание к лаб.4 - `RAID0`

### Аппаратная реализация на x86_64

Два HDD диска от Seagate подключаются по SATA3 к материнской плате Х99 v205 на сокете LGA 2011v3. Операции производятся процессором Xeon Е5-2630v3, на плате установлено 32 гигабайта ECC DDR4 оперативной памяти, работающей на частоте 2400 Мгц.

|![image](https://github.com/user-attachments/assets/57bb1f4a-2043-4623-9db1-47a025b8443a)|
|-|

### Теория

RAID 0 работает так: данные делятся на страйпы (обычно 64KB) и записываются поочерёдно на два диска: первый блок на диск A, второй на диск B и так далее. При записи оба диска работают параллельно, что увеличивает пропускную способность. Прирост есть только в скорости записи (и иногда последовательного чтения), так как данные записываются/читаются одновременно с двух дисков. Избыточности нет — выход из строя одного диска уничтожает все данные.

|![image](https://github.com/user-attachments/assets/53618427-77aa-4d69-a55e-fef2c51cab24)|
|-|


### Программная реализация массива raid0 на ОС windows 10

|![image_2024-12-19_23-14-41](https://github.com/user-attachments/assets/63b5521a-48b8-4ecb-8c26-877f5eb1985f)|
|-|

### Профилирование

> [!NOTE]
> Видим жёский прирост на записи, спасибо raid0

|jbod (just hdd)|raid0 array|
|-|-|
|![image_2024-12-19_23-18-47](https://github.com/user-attachments/assets/9b814864-d75f-48c5-a412-aa5b93628c69)|![image_2024-12-19_23-11-58](https://github.com/user-attachments/assets/da892e34-ef42-4ea1-adfd-2005d834f788)|


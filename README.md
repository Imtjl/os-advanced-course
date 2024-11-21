# Форк учебной ОС Xv6 для ITMO CSE

> [!IMPORTANT]
> Этот репозиторий **не содержит решений** и любых подсказок к решениям лабораторных работ.
> Он создан исключительно для **образовательных целей** - информирования, хранения доп. обучающих материалов.
> Вся работа выполнялась в соответствующем приватном клоне данного репозитория.

## Начало работы

1. [Первый запуск ОС Xv6: Linux](/doc/setup/linux.md)

2. [Настройка IDE: VSCode](/doc/dev/vscode.md)

3. [Подготовка репозитория](/doc/setup/repo.md)

## Лабораторные работы

1. [Задание 1. Введение в Xv6](/doc/lab/1.md)

2. [Задание 2. Аллокатор](/doc/lab/2.md)

3. [Задание 3. Copy-on-write fork](/doc/lab/3.md)

## Доп. задание к лаб.1

Сделать утилиту для вывода системной информации => `sysinfo`

- executing `dump2tests`:
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

- idle
```fish
$ sysinfo
System info:
 Number of procs: 3
 Number of open files: 1
```

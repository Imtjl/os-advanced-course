// What this module does essentially?
// It's out custom virtual filesystem implementation, like ext4, NFS or others
// You can find it's creation in struct vtfs_fs_type

// It provides an interface for different operations:
// - lookup (find file / directory by name)
// - create/unlink (create/delete file from RAM)
// - mkdir/rmdir (create/remove directory from RAM)
// - read/write/iterate

// Why do we even need those operations?
// So that you can do "ls", "cd", "rm", "cp" and other shell commands, that call those operations.

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

void encode(const char* src, char* dst) {
  while (*src != '\0') {
    if ((*src >= '0' && *src <= '9') || (*src >= 'a' && *src <= 'z') ||
        (*src >= 'A' && *src <= 'Z')) {
      *dst = *src;
      dst++;
    } else {
      sprintf(dst, "%%%02X", (unsigned char)*src);
      dst += 3;
    }
    src++;
  }
  *dst = '\0';
}
#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

unsigned long next_ino = ROOT_INODE_INO;

struct inode* vtfs_get_inode(struct super_block*, const struct inode*, umode_t, int);

struct vtfs_inode {
  int ino;            // Номер inode (наш внутренний)
  umode_t mode;       // Права доступа, тип (директория / файл)
  size_t i_size;      // Сколько байт данных (для файла)
  char i_data[1024];  // Собственно, содержимое файла (до 1 КБ)
};

// Discovery entry
// links filename with it's inode
struct vtfs_dentry {
  struct dentry* d_dentry;  // Ссылка на ядровую dentry (которая хранит имя и связь с VFS)
  char d_name[256];  // Имя файла/директории (например, "hello.txt")
  int d_parent_ino;  // Номер inode родительской директории
  struct vtfs_inode* d_inode;  // Ссылка на нашу vtfs_inode (данные)
  struct list_head list;       // Для включения в общий список (в vtfs_sb)
};

struct {
  struct super_block* sb;     // Ссылка на ядровый super_block
  struct list_head dentries;  // Двусвязный список всех "vtfs_dentry"
} vtfs_sb;

// Find child file or dir in vtfs_sb.dentries
struct dentry* vtfs_lookup(
    struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag
) {
  // ----------------|  RAM  |---------------------

  struct vtfs_dentry* dentry;
  struct list_head* pos;
  struct inode* inode;

  list_for_each(pos, &vtfs_sb.dentries) {
    dentry = list_entry(pos, struct vtfs_dentry, list);

    if (dentry->d_parent_ino == parent_inode->i_ino &&
        strcmp(dentry->d_name, child_dentry->d_name.name) == 0) {
      inode = vtfs_get_inode(vtfs_sb.sb, parent_inode, dentry->d_inode->mode, dentry->d_inode->ino);
      d_add(child_dentry, inode);
      return NULL;
    }
  }
  return NULL;  // if nothing is found
};

// Create file in RAM
int vtfs_create(struct inode* parent_inode, struct dentry* child_dentry, umode_t mode, bool b) {
  // ----------------|  RAM  |---------------------

  struct inode* inode;
  struct vtfs_dentry* new_dentry;
  struct vtfs_inode* new_inode;

  inode = vtfs_get_inode(vtfs_sb.sb, parent_inode, mode, next_ino++);
  if (!inode) {
    return -ENOMEM;
  }

  new_dentry = kmalloc(sizeof(struct vtfs_dentry), GFP_KERNEL);
  if (!new_dentry) {
    iput(inode);
    return -ENOMEM;
  }

  new_inode = kmalloc(sizeof(struct vtfs_inode), GFP_KERNEL);
  if (!new_inode) {
    kfree(new_dentry);
    iput(inode);
    return -ENOMEM;
  }

  new_dentry->d_dentry = child_dentry;
  strcpy(new_dentry->d_name, child_dentry->d_name.name);
  new_dentry->d_parent_ino = parent_inode->i_ino;
  new_dentry->d_inode = new_inode;
  new_dentry->d_inode->ino = inode->i_ino;
  new_dentry->d_inode->mode = inode->i_mode;
  new_dentry->d_inode->i_size = 0;

  list_add(&new_dentry->list, &vtfs_sb.dentries);

  d_add(child_dentry, inode);

  printk(KERN_INFO "File %s added successfully\n", child_dentry->d_name.name);

  return 0;
}

// Delete file from RAM
int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry) {
  // ----------------|  RAM  |---------------------

  struct vtfs_dentry* found_dentry = NULL;
  struct list_head* pos;

  list_for_each(pos, &vtfs_sb.dentries) {
    found_dentry = list_entry(pos, struct vtfs_dentry, list);

    if (strcmp(found_dentry->d_name, child_dentry->d_name.name) == 0 &&
        found_dentry->d_parent_ino == parent_inode->i_ino) {
      list_del(&found_dentry->list);

      kfree(found_dentry);

      printk(KERN_INFO "File %s deleted successfully\n", child_dentry->d_name.name);

      return 0;
    }
  }

  return -ENOENT;
}

// Create dir in RAM
int vtfs_mkdir(struct inode* parent_inode, struct dentry* child_dentry, umode_t mode) {
  // ----------------|  RAM  |---------------------

  struct inode* inode;
  struct vtfs_dentry* new_dentry;

  inode = vtfs_get_inode(vtfs_sb.sb, parent_inode, mode | S_IFDIR, next_ino++);
  if (!inode) {
    return -ENOMEM;
  }

  new_dentry = kmalloc(sizeof(struct vtfs_dentry), GFP_KERNEL);
  if (!new_dentry) {
    iput(inode);
    return -ENOMEM;
  }

  new_dentry->d_inode = kmalloc(sizeof(struct vtfs_inode), GFP_KERNEL);
  if (!new_dentry->d_inode) {
    kfree(new_dentry);
    iput(inode);
    return -ENOMEM;
  }

  new_dentry->d_dentry = child_dentry;
  strcpy(new_dentry->d_name, child_dentry->d_name.name);
  new_dentry->d_parent_ino = parent_inode->i_ino;
  new_dentry->d_inode->ino = inode->i_ino;
  new_dentry->d_inode->mode = inode->i_mode;
  new_dentry->d_inode->i_size = 0;

  list_add(&new_dentry->list, &vtfs_sb.dentries);

  d_add(child_dentry, inode);

  printk(KERN_INFO "Directory %s created successfully\n", child_dentry->d_name.name);

  return 0;
}

// Delete dir from RAM
int vtfs_rmdir(struct inode* parent_inode, struct dentry* child_dentry) {
  // ----------------|  RAM  |---------------------

  struct vtfs_dentry* found_dentry = NULL;
  struct list_head* pos;

  list_for_each(pos, &vtfs_sb.dentries) {
    found_dentry = list_entry(pos, struct vtfs_dentry, list);

    if (strcmp(found_dentry->d_name, child_dentry->d_name.name) == 0 &&
        found_dentry->d_parent_ino == parent_inode->i_ino) {
      list_del(&found_dentry->list);
      kfree(found_dentry);

      printk(KERN_INFO "File %s deleted successfully\n", child_dentry->d_name.name);

      return 0;
    }
  }

  return -ENOENT;  // couldn't find any dir
}

struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .unlink = vtfs_unlink,
    .mkdir = vtfs_mkdir,
    .rmdir = vtfs_rmdir,
};

// Iterate through all dir includes in RAM
int vtfs_iterate(struct file* file, struct dir_context* ctx) {
  struct vtfs_dentry* dentry;
  struct list_head* pos;
  struct inode* dir_inode = file->f_path.dentry->d_inode;
  unsigned char type;

  if (!dir_emit_dots(file, ctx))
    return 0;

  if (ctx->pos >= 3) {
    return ctx->pos;
  }

  list_for_each(pos, &vtfs_sb.dentries) {
    dentry = list_entry(pos, struct vtfs_dentry, list);

    printk(
        KERN_INFO "Dentry %s inode %ld data %s\n",
        dentry->d_name,
        dentry->d_inode->ino,
        dentry->d_inode->i_data
    );

    if (S_ISDIR(dentry->d_inode->mode))
      type = DT_DIR;
    else if (S_ISREG(dentry->d_inode->mode))
      type = DT_REG;
    else
      type = DT_UNKNOWN;

    if (dentry->d_parent_ino == dir_inode->i_ino &&
        !dir_emit(ctx, dentry->d_name, strlen(dentry->d_name), dentry->d_inode->ino, type)) {
      return -ENOMEM;
    }

    ctx->pos += 1;
  }

  return ctx->pos;
}

// Read file data from RAM
ssize_t vtfs_read(struct file* file, char* buffer, size_t len, loff_t* offset) {
  // ----------------|  RAM  |---------------------

  struct vtfs_inode* found_inode;
  struct vtfs_dentry* found_dentry;
  struct inode* file_inode = file->f_inode;
  struct list_head* pos;
  ssize_t to_read;

  list_for_each(pos, &vtfs_sb.dentries) {
    found_dentry = list_entry(pos, struct vtfs_dentry, list);
    found_inode = found_dentry->d_inode;

    if (found_dentry->d_inode->ino == file_inode->i_ino) {
      if (*offset > found_inode->i_size)
        return 0;

      to_read = min(len, found_inode->i_size - *offset);
      if (copy_to_user(buffer, found_inode->i_data + *offset, to_read))
        return -EFAULT;

      *offset += to_read;

      return to_read;
    }
  }

  return -ENOENT;
}

// write file data to RAM
ssize_t vtfs_write(struct file* file, const char* buffer, size_t len, loff_t* offset) {
  // ----------------|  RAM  |---------------------

  struct vtfs_inode* found_inode;
  struct vtfs_dentry* found_dentry;
  struct inode* file_inode = file->f_inode;
  struct list_head* pos;
  void* new_data;
  ssize_t new_size;

  list_for_each(pos, &vtfs_sb.dentries) {
    found_dentry = list_entry(pos, struct vtfs_dentry, list);
    found_inode = found_dentry->d_inode;

    if (found_dentry->d_inode->ino == file_inode->i_ino) {
      new_size = max(found_inode->i_size, *offset + len);

      if (copy_from_user(found_inode->i_data + *offset, buffer, len)) {
        return -EFAULT;
      }

      found_inode->i_size = new_size;

      *offset += len;

      return len;
    }
  }

  return -ENOENT;
}

struct file_operations vtfs_dir_ops = {
    .iterate = vtfs_iterate,
    .read = vtfs_read,
    .write = vtfs_write,
};

struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
) {
  struct inode* inode = new_inode(sb);
  if (inode != NULL) {
    inode_init_owner(inode, dir, mode);
  }

  inode->i_ino = i_ino;
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_dir_ops;

  inc_nlink(inode);

  return inode;
}

int vtfs_fill_super(struct super_block* sb, void* data, int silent) {
  umode_t mode = S_IFDIR | 0777;

  struct inode* inode = vtfs_get_inode(sb, NULL, mode, ROOT_INODE_INO);
  if (!inode) {
    printk(KERN_ERR "Failed to create a root inode");
    return -ENOMEM;
  }

  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    printk(KERN_ERR "Failed to create a root dentry");
    return -ENOMEM;
  }

  vtfs_sb.sb = sb;
  INIT_LIST_HEAD(&vtfs_sb.dentries);

  printk(KERN_INFO "return 0\n");

  return 0;
}

struct dentry* vtfs_mount(
    struct file_system_type* fs_type, int flags, const char* token, void* data
) {
  struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
  if (ret == NULL) {
    printk(KERN_ERR "Can't mount file system");
  } else {
    printk(KERN_INFO "Mounted successfuly");
  }
  return ret;
}

int init(void) {
  next_ino = ROOT_INODE_INO;
  return 0;
}

void vtfs_kill_sb(struct super_block* sb) {
  printk(KERN_INFO "vtfs super block is destroyed. Unmount successfully.\n");
}

struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
};

static int __init vtfs_init(void) {
  LOG("VTFS joined the kernel\n");
  return 0;
}

static void __exit vtfs_exit(void) {
  LOG("VTFS left the kernel\n");
}

module_init(vtfs_init);
module_exit(vtfs_exit);

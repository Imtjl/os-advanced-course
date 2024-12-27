#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "http.h"

const char* SERVER_IP = "0.0.0.0";
const int SERVER_PORT = 8080;

// callee should call free_request on received buffer
int fill_request(
    struct kvec* vec, const char* token, const char* method, size_t arg_size, va_list args
) {
  // 2048 bytes for URL and 64 bytes for anything else
  char* request_buffer = kzalloc(2048 + 64, GFP_KERNEL);
  if (request_buffer == 0) {
    return -ENOMEM;
  }

  strcpy(request_buffer, "GET /api/");
  strcat(request_buffer, method);

  strcat(request_buffer, "?token=");
  strcat(request_buffer, token);

  int i;
  for (i = 0; i < arg_size; i++) {
    strcat(request_buffer, "&");
    strcat(request_buffer, va_arg(args, char*));
    strcat(request_buffer, "=");
    strcat(request_buffer, va_arg(args, char*));
  }

  strcat(request_buffer, " HTTP/1.1\r\nHost:");
  strcat(request_buffer, SERVER_IP);
  strcat(request_buffer, "\r\nConnection: close\r\n\r\n");

  memset(vec, 0, sizeof(struct kvec));
  vec->iov_base = request_buffer;
  vec->iov_len = strlen(request_buffer);

  return 0;
}

int receive_all(struct socket* sock, char* buffer, size_t buffer_size) {
  struct msghdr hdr;
  struct kvec vec;

  int read = 0;

  while (read < buffer_size) {
    memset(&hdr, 0, sizeof(struct msghdr));
    memset(&vec, 0, sizeof(struct kvec));
    vec.iov_base = buffer + read;
    vec.iov_len = buffer_size - read;
    int ret = kernel_recvmsg(sock, &hdr, &vec, 1, vec.iov_len, 0);
    if (ret == 0) {
      break;
    } else if (ret < 0) {
      return -4;
    }
    read += ret;
  }

  return read;
}

int64_t parse_http_response(
    char* raw_response, size_t raw_response_size, char* response, size_t response_size
) {
  char* buffer = raw_response;

  // Read Response Line
  {
    char* status_line = strsep(&buffer, "\r");
    strsep(&status_line, " ");
    if (status_line == 0) {
      return -6;
    }
    char* status_code = strsep(&status_line, " ");
    printk(KERN_INFO "Received response with status code %s\n", status_code);
    if (strcmp(status_code, "200") != 0) {
      return -5;
    }
  }

  int length = -1;

  while (true) {
    if (buffer == 0) {
      return -6;
    }
    char* header = strsep(&buffer, "\r");
    ++header;  // skip \n
    if (strcmp(header, "") == 0) {
      // end of headers
      break;
    }

    if (strncmp(header, "Content-Length: ", 16) == 0) {
      int error = kstrtoint(header + 16, 0, &length);
      if (error != 0) {
        return -6;
      }
      printk(KERN_INFO "Received response with content length %d\n", length);
    }
  }
  ++buffer;  // skip last '\n'

  if (length == -1) {
    return -6;
  }

  if (buffer + length > raw_response + raw_response_size) {
    return -6;
  }

  if (length < sizeof(int64_t)) {
    return -7;
  }

  length -= sizeof(int64_t);

  if (length > response_size) {
    return -ENOSPC;
  }

  int64_t return_value;
  memcpy(&return_value, buffer, sizeof(int64_t));

  buffer += sizeof(int64_t);
  memcpy(response, buffer, length);

  return return_value;
}

int64_t vtfs_http_call(
    const char* token,
    const char* method,
    char* response_buffer,
    size_t buffer_size,
    size_t arg_size,
    ...
) {
  struct socket* sock;
  int64_t error;

  error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
  if (error < 0) {
    return -1;
  }

  struct sockaddr_in s_addr = {
      .sin_family = AF_INET,
      .sin_addr = {.s_addr = in_aton(SERVER_IP)},
      .sin_port = htons(SERVER_PORT)
  };

  error = kernel_connect(sock, (struct sockaddr*)&s_addr, sizeof(struct sockaddr_in), 0);
  if (error != 0) {
    sock_release(sock);
    return -2;
  }

  struct kvec kvec;
  va_list args;
  va_start(args, arg_size);
  error = fill_request(&kvec, token, method, arg_size, args);
  va_end(args);

  if (error != 0) {
    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    return error;
  }

  struct msghdr msg;
  memset(&msg, 0, sizeof(struct msghdr));

  error = kernel_sendmsg(sock, &msg, &kvec, 1, kvec.iov_len);
  kfree(kvec.iov_base);

  if (error < 0) {
    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    return -3;
  }

  size_t raw_buffer_size = buffer_size + 1024;  // add 1KB for HTTP headers
  char* raw_response_buffer = kmalloc(raw_buffer_size, GFP_KERNEL);
  if (raw_response_buffer == 0) {
    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    return -ENOMEM;
  }
  int read_bytes = receive_all(sock, raw_response_buffer, raw_buffer_size);

  kernel_sock_shutdown(sock, SHUT_RDWR);
  sock_release(sock);

  if (read_bytes < 0) {
    kfree(raw_response_buffer);
    return -4;
  }

  error = parse_http_response(raw_response_buffer, read_bytes, response_buffer, buffer_size);

  kfree(raw_response_buffer);
  return error;
}

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

struct __attribute__((__packed__)) lookup_response {
  uint32_t ino;
  uint8_t mode;
};

struct __attribute__((__packed__)) create_response {
  uint32_t ino;
};

struct __attribute__((__packed__)) remove_response {};

struct __attribute__((__packed__)) iterate_response {
  uint32_t count;
  struct __attribute__((__packed__)) r_dentry {
    char name[256];
    uint32_t ino;
    uint8_t mode;
  } r_dentries[8];
};

struct __attribute__((__packed__)) read_response {
  uint32_t size;
  char data[2048];
};

struct __attribute__((__packed__)) write_response {};

struct __attribute__((__packed__)) max_ino_response {
  uint32_t ino;
};

#define MODULE_NAME "vtfs"

#define ROOT_INODE_INO 100

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

unsigned long next_ino = ROOT_INODE_INO;

struct inode* vtfs_get_inode(struct super_block*, const struct inode*, umode_t, int);

struct vtfs_inode {
  int ino;
  umode_t mode;
  size_t i_size;
  char i_data[1024];
};

struct vtfs_dentry {
  struct dentry* d_dentry;
  char d_name[256];
  int d_parent_ino;
  struct vtfs_inode* d_inode;
  struct list_head list;
};

struct {
  struct super_block* sb;
  struct list_head dentries;
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
  register_filesystem(&vtfs_fs_type);

  if (init() != 0) {
    LOG("VTFS could not join the kernel\n");
    return -1;
  }

  LOG("VTFS joined the kernel\n");
  return 0;
}

static void __exit vtfs_exit(void) {
  unregister_filesystem(&vtfs_fs_type);
  LOG("VTFS left the kernel\n");
}

module_init(vtfs_init);
module_exit(vtfs_exit);

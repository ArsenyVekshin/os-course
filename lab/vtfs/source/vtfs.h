#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/dcache.h>
#include <linux/uaccess.h>

#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)
#define FULL_ACCESS_RIGHTS 0777


static struct vtfs_dentry* vtfs_find_by_name(ino_t parent_ino, const char* name);
static struct vtfs_dentry* vtfs_find_by_ino(ino_t ino);

struct dentry* vtfs_mount(struct file_system_type*, int, const char*, void*);
void vtfs_kill_sb(struct super_block*);
struct dentry* vtfs_lookup(struct inode*, struct dentry*, unsigned int);
int vtfs_create(struct inode*, struct dentry*, umode_t, bool);
int vtfs_unlink(struct inode*, struct dentry*);
int vtfs_iterate(struct file*, struct dir_context*);
int vtfs_fill_super(struct super_block*, void*, int);
struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);

int vtfs_mkdir(struct inode*, struct dentry*, umode_t);
int vtfs_rmdir(struct inode*, struct dentry*);


ssize_t vtfs_read(struct file*, char __user*, size_t, loff_t*);
ssize_t vtfs_write(struct file*, const char __user*, size_t, loff_t*);

int vtfs_link(struct dentry*, struct inode*, struct dentry*);

ino_t vtfs_ino_counter = 100;

// Запись INODE
struct vtfs_inode {
	umode_t i_mode;   	// Режим доступа
  	ino_t i_ino;      	// Уникальный идентификатор inode
  	size_t i_size;    	// Размер файла
  	nlink_t i_nlink;  	// Количество жестких ссылок
  	char i_data[1024]; 	// Данные файла (ограничены 1024 байтами)
};

// Элемент каталога
struct vtfs_dentry {
	char d_name[128];      		// Имя файла/каталога
	ino_t d_parent_ino;    		// Inode родительского каталога
	struct vtfs_inode *d_inode;	// Указатель на inode
	struct list_head d_list; 	// Указатели на соседние элементы массива
};

// Cостояние файловой системы
static struct vtfs_sb_note{
	struct super_block *s_sb; 	// Суперблок (корневая папка) 
	struct list_head content; 	// Двусвязный массив содержимого
} vtfs_sb = {
	.s_sb = NULL,
	.content = LIST_HEAD_INIT(vtfs_sb.content),
};

// Аргументы для инициализации системы
struct file_system_type vtfs_fs_type = {
	.name = "vtfs",
	.mount = vtfs_mount,
	.kill_sb = vtfs_kill_sb,
};

// внешнее "api" для inode
struct inode_operations vtfs_inode_ops = {
	.lookup = vtfs_lookup,
	.create = vtfs_create,
	.unlink = vtfs_unlink,
	.mkdir = vtfs_mkdir,
	.rmdir = vtfs_rmdir,
	.link = vtfs_link,
};


// Операции с каталогами
struct file_operations vtfs_dir_ops = {
	.iterate = vtfs_iterate,
};

// Операции с файлами
struct file_operations vtfs_file_ops = {
	.read = vtfs_read,
	.write = vtfs_write,
};

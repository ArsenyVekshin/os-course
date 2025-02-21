#include "vtfs.h"

// Поиск записи по директории + имени
static struct vtfs_dentry* vtfs_find_by_name(ino_t parent_ino, const char* name) {
	struct vtfs_dentry *curr;
	list_for_each_entry(curr, &vtfs_sb.content, d_list) {
		if (curr->d_parent_ino == parent_ino && strcmp(curr->d_name, name) == 0)
			return curr;
	}
	return NULL;
}

// Поиск файла номеру inode
static struct vtfs_dentry* vtfs_find_by_ino(ino_t ino) {
	struct vtfs_dentry *curr;
	list_for_each_entry(curr, &vtfs_sb.content, d_list) {
		if (curr->d_inode->i_ino == ino) return curr;
	}
	return NULL;
}


struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino) {
	struct inode *inode = new_inode(sb);
	if (inode != NULL) {
		inode_init_owner(inode, dir, mode);
		inode->i_sb = sb;
		inode->i_ino = i_ino;
		inode->i_op = &vtfs_inode_ops;
		if (S_ISDIR(mode)) inode->i_fop = &vtfs_dir_ops;
		else  inode->i_fop = &vtfs_file_ops;
	}

	return inode;
}



int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
	vtfs_sb.s_sb = sb;

	struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR | 0777, vtfs_ino_counter++);
	if (inode == NULL) return -ENOMEM; 

	inode->i_fop = &vtfs_dir_ops;
	sb->s_root = d_make_root(inode);
	if (sb->s_root == NULL) {
		iput(inode); // освободим установленную inode
		return -ENOMEM; 
	}
	
	return 0;
}


struct dentry* vtfs_lookup(struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag) {
	struct vtfs_dentry* curr = vtfs_find_by_name(parent_inode->i_ino, child_dentry->d_name.name);
	if (curr == NULL) return NULL;

	struct inode* inode = vtfs_get_inode(parent_inode->i_sb, parent_inode, curr->d_inode->i_mode, curr->d_inode->i_ino);
	if (inode == NULL) return -ENOMEM;

	d_add(child_dentry, inode); //добавляем в dentry запись inode
	return NULL;
}

int vtfs_iterate(struct file *file, struct dir_context *dir_ctx) {
	struct inode* inode = file_inode(file);
    ino_t parent_ino = inode->i_ino;
    struct vtfs_dentry *curr;

	if (dir_ctx->pos == 0) { // Имитируем "."
		if (!dir_emit_dot(file, dir_ctx)) return 0;
		dir_ctx->pos++;
	}
	if (dir_ctx->pos == 1) { // Имитируем ".."
		if (!dir_emit_dotdot(file, dir_ctx)) return 0;
		dir_ctx->pos++;
	}

	long i = 2;
	list_for_each_entry(curr, &vtfs_sb.content, d_list) {
		if (curr->d_parent_ino != parent_ino) continue;
		if (dir_ctx->pos > i){
			i++;
			continue;
		}
		if (!dir_emit(dir_ctx, curr->d_name, strlen(curr->d_name), curr->d_inode->i_ino, DT_UNKNOWN)) 
			return 0;
		dir_ctx->pos++;
		i++;
	}
	return 0;
}


int vtfs_create(struct inode *parent_inode, struct dentry *child_dentry, umode_t mode, bool b) {
	struct vtfs_dentry *dentry = kmalloc(sizeof(*dentry), GFP_KERNEL);
	if (dentry == NULL) return -ENOMEM;
	memset(dentry, 0, sizeof(*dentry));

	struct vtfs_inode *inode = kmalloc(sizeof(*inode), GFP_KERNEL);
	if (inode == NULL) { kfree(dentry); return -ENOMEM;}
	memset(inode, 0, sizeof(*inode));

	inode->i_size = 0;
	inode->i_mode = mode;
	inode->i_ino = vtfs_ino_counter++;
	
	dentry->d_inode = inode;
	dentry->d_parent_ino = parent_inode->i_ino;
	strncpy(dentry->d_name, child_dentry->d_name.name, sizeof(child_dentry->d_name)-1);

	list_add(&dentry->d_list, &vtfs_sb.content);

	struct inode* sys_inode = vtfs_get_inode(parent_inode->i_sb, parent_inode, mode, inode->i_ino);
	if (!sys_inode) {
		list_del(&dentry->d_list);
		kfree(inode);
		kfree(dentry);
		printk(KERN_ERR "vtfs_create: ошибка при получении inode \n"); 
		return -ENOMEM;
	}

	d_add(child_dentry, sys_inode);
	return 0;
}

int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry) {
	struct vtfs_dentry* curr = vtfs_find_by_name(parent_inode->i_ino, child_dentry->d_name.name);
	if (curr == NULL) return -ENOENT;

	list_del(&curr->d_list);
	
	curr->d_inode->i_nlink--;
	if (curr->d_inode->i_nlink <= 0) kfree(curr->d_inode);
	kfree(curr);

	return 0;	
}

int vtfs_mkdir(struct inode *parent_inode, struct dentry *child_dentry, umode_t mode) {
	mode |= S_IFDIR;
	return vtfs_create(parent_inode, child_dentry, mode, false);
}

int vtfs_rmdir(struct inode *parent_inode, struct dentry *child_dentry) {
	struct vtfs_dentry* curr = vtfs_find_by_name(parent_inode->i_ino, child_dentry->d_name.name);
	if (curr == NULL) return -ENOENT;

	// Директория?
	if (!S_ISDIR(curr->d_inode->i_mode)) return -ENOENT;

	// Пустая?
	struct file fake_file = {};
    fake_file.f_inode = curr->d_inode->i_ino;
	struct dir_context dir_context = {0};
	if (vtfs_iterate(&fake_file, &dir_context) > 2) return -ENOTEMPTY;
	
	list_del(&curr->d_list);
	
	curr->d_inode->i_nlink--;
	if (curr->d_inode->i_nlink <= 0) kfree(curr->d_inode);
	kfree(curr);

	return 0;	
}

ssize_t vtfs_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset) {
	struct vtfs_dentry* curr = vtfs_find_by_ino(filp->f_inode->i_ino);
	if (!curr) return -ENOENT;

	struct vtfs_inode* inode = curr->d_inode;
	if (!inode) return -ENOENT;

	if (inode->i_size > 0 && *offset >= inode->i_size) return 0; // Конец файла

	size_t to_read = len;
	if (*offset + len > inode->i_size) to_read = inode->i_size - *offset;

	// Отправляем данные пользователю
	int res = copy_to_user(buffer, inode->i_data + *offset, to_read);
	if (res) return -EFAULT;  

	*offset += to_read;

	return to_read;
}

ssize_t vtfs_write(struct file *filp, const char __user *buffer, size_t len, loff_t *offset) {
    struct vtfs_dentry* curr = vtfs_find_by_ino(filp->f_inode->i_ino);
    if (!curr) return -ENOENT;

    struct vtfs_inode* inode = curr->d_inode;
    if (!inode) return -ENOENT;

    // Ограничение записи в пределах буфера
    size_t max_size = sizeof(inode->i_data);
    if (*offset >= max_size) return -ENOSPC;  // Нет места

    size_t to_write = len;
    if (*offset + len > max_size) 
        to_write = max_size - *offset;

    // Копируем данные от пользователя
    if (copy_from_user(inode->i_data + *offset, buffer, to_write)) 
        return -EFAULT;

    *offset += to_write;

    // Обновляем размер файла, если записали дальше, чем раньше
    if (*offset > inode->i_size) 
        inode->i_size = *offset;

    return to_write;
}

int vtfs_link(struct dentry *old_dentry, struct inode *parent_dir, struct dentry *new_dentry) {
	struct vtfs_dentry *vtfs_old = vtfs_find_by_name(old_dentry->d_parent->d_inode->i_ino, old_dentry->d_name.name);
	if (vtfs_old == NULL)  return -ENOENT; 

	if (S_ISDIR(vtfs_old->d_inode->i_mode))  return -EPERM; 

	struct vtfs_dentry *vtfs_new = kmalloc(sizeof(vtfs_new), GFP_KERNEL);
	if (vtfs_new == NULL)  return -ENOENT; 
	memset(vtfs_new, 0, sizeof(*vtfs_new));


	struct vtfs_inode *inode = vtfs_old->d_inode;
	vtfs_new->d_inode = inode;
	vtfs_new->d_parent_ino = parent_dir->i_ino;
	strncpy(vtfs_new->d_name, new_dentry->d_name.name, sizeof(vtfs_new->d_name) - 1);

	inode->i_nlink++;
	list_add(&vtfs_new->d_list, &vtfs_sb.content);

	struct inode *new_inode = vtfs_get_inode(parent_dir->i_sb, parent_dir, inode->i_mode, inode->i_ino);
	if (!new_inode) {
		list_del(&vtfs_new->d_list);
		kfree(vtfs_new);
		return -ENOMEM;
	}

	d_add(new_dentry, new_inode);
	return 0;
}

#pragma region file_system_type funcs

struct dentry* vtfs_mount(struct file_system_type* fs_type, int flags, const char* token, void* data) {
	struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
	if (ret == NULL) printk(KERN_ERR "Can't mount file system");
	else  printk(KERN_INFO "Mounted successfuly"); 

	return ret;
}


void vtfs_kill_sb(struct super_block* sb) {
	struct vtfs_dentry *curr, *tmp;

	list_for_each_entry_safe(curr, tmp, &vtfs_sb.content, d_list) {
		list_del(&curr->d_list);
		kfree(curr->d_inode);
		kfree(curr);
	}

	kill_anon_super(sb);
	printk(KERN_INFO "vtfs super block is destroyed. Unmount successfully.\n");
}
#pragma endregion

static int __init vtfs_init(void) {
	register_filesystem(&vtfs_fs_type);
  	LOG("VTFS joined the kernel\n");
  	return 0;
}

static void __exit vtfs_exit(void) {
	unregister_filesystem(&vtfs_fs_type);
	LOG("VTFS left the kernel\n");
	return 0;
}

module_init(vtfs_init);
module_exit(vtfs_exit);

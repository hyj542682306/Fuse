#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

static const int maxlength_of_filename = 10;

char *debug;

static struct node {
	char *filename;
	char *contents;

	int dir_nod; // 2: dir   1: nod
	
	struct node *nxt;
	struct node *pre;
	
	struct node *son; // first son
	struct node *father;
};
static struct node *root;
static struct node *log;

static struct node *create(char *_filename, char *_contents, int _dir_nod, struct node *_nxt,
			struct node *_pre, struct node *_son, struct node* _father)
{
	struct node *ptr = malloc(sizeof(struct node));
	memset(ptr, 0, sizeof(struct node));

	ptr->filename = strdup(_filename);
	ptr->contents = strdup(_contents);
	ptr->dir_nod = _dir_nod;
	ptr->nxt = _nxt;
	ptr->pre = _pre;
	ptr->son = _son;
	ptr->father = _father;

	return ptr;
}

static void add(struct node *f, struct node *ptr)
{
	ptr->father = f;

	struct node *p = f;
	
	if(p->son == NULL) {
		p->son = ptr;
	} else {
		p = p->son;
		while(p->nxt != NULL) p = p->nxt;
		p->nxt = ptr;
		ptr->pre = p;
	}
}

static void del(struct node *ptr)
{
	if(ptr->pre != NULL && ptr->nxt != NULL) {
		ptr->pre->nxt = ptr->nxt;
		ptr->nxt->pre = ptr->pre;
	} else if(ptr->pre != NULL) {
		ptr->pre->nxt = NULL;
	} else if(ptr->nxt != NULL) {
		ptr->father->son = ptr->nxt;
		ptr->nxt->pre = NULL;
	} else {
		ptr->father->son = NULL;
	}
}

static int found(const char *path, struct node **nod)
{
	struct node *pointer = root;

	int pos = 1;
	
	while(path[pos] != '\0'){
		char nowname[maxlength_of_filename];
		int namelen=0;

		while(path[pos] != '\0' && path[pos] != '/')
			nowname[namelen++] = path[pos++];
		nowname[namelen] = '\0';

		int dir_nod = 0;
		if(path[pos] == '/'){
			dir_nod = 2;
			pos++;
		}

		if(pointer->son == NULL) return -ENOENT;
		pointer = pointer->son;

		int flag = 0;
		while(pointer!=NULL){
			if(strcmp(pointer->filename, nowname)==0){
				if(dir_nod != 0 && dir_nod != pointer->dir_nod) return -ENOENT;
				flag = 1;
				break;
			}
			pointer = pointer->nxt;
		}
		if(!flag) return -ENOENT;
	}
	
	(*nod) = pointer;
	return 0;
}

static void *chat_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;
	cfg->kernel_cache = 0;
	return NULL;
}

static int chat_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	(void) fi;

	memset(stbuf, 0, sizeof(struct stat));

	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;

	if (nod->dir_nod == 2) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		stbuf->st_mode = S_IFREG | 0770;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(nod->contents);
	}

	return 0;
}

static int chat_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;


	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	nod = nod->son;
	while(nod != NULL){
		filler(buf, nod->filename, NULL, 0, 0);
		nod = nod->nxt;
	}

	return 0;
}

static int chat_open(const char *path, struct fuse_file_info *fi)
{
	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;

	if(nod->dir_nod != 1) return -ENOENT;

	return 0;
}

static int chat_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;

	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;

	if(nod->dir_nod != 1) return -ENOENT;

	len = strlen(nod->contents);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, nod->contents + offset, size);
	} else
		size = 0;

	return size;
}

static int chat_access(const char *path, int mask)
{
	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;

	if(nod->dir_nod != 2) return -ENOENT;

	return 0;
}

static int chat_write(const char *path, const char *buf, size_t size, off_t offset,
				struct fuse_file_info *fi)
{
	(void) fi;

	int pathlen = strlen(path);

	char user1[maxlength_of_filename], user2[maxlength_of_filename];
	char *envi = (char *) malloc(pathlen + 1);
	int userlen1 = 0, userlen2 = 0, envilen = 0;
	
	// example:
	// /mnt/bot1/bot2
	// user1: bot2
	// user2: bot1
	// envi: /mnt/

	if(path[pathlen - 1] == '/') return -ENOENT;

	int st = pathlen;
	while(st > 0 && path[st - 1] != '/') st--;
	if(st <= 0) return -ENOENT;
	while(st + userlen1 <= pathlen - 1) {
		user1[userlen1] = path[st + userlen1];
		userlen1++;
	}
	user1[userlen1] = '\0';

	st--;
	while(st > 0 && path[st - 1] != '/') st--;
	if(st <= 0) return -ENOENT;
	while(st + userlen2 <= pathlen - userlen1 - 2) {
		user2[userlen2] = path[st + userlen2];
		userlen2++;
	}
	user2[userlen2] = '\0';

	while(envilen <= st - 1) {
		envi[envilen] = path[envilen];
		envilen++;
	} 
	envi[envilen] = '\0';

	char *path1 = (char *) malloc(pathlen + 1);
	char *path2 = (char *) malloc(pathlen + 1);
	memcpy(path1, path, pathlen);
	path1[pathlen] = '\0';
	for(int i = 0; i < envilen; ++i) path2[i] = envi[i];
	for(int i = 0; i < userlen1; ++i) path2[envilen + i] = user1[i];
	path2[envilen + userlen1] = '/';
	for(int i = 0; i < userlen2; ++i) path2[envilen + userlen1 + 1 + i] = user2[i];
	path2[pathlen] = '\0';

	struct node *nod1, *nod2;
	if(found(path1, &nod1) != 0) return -ENOENT;
	
	if(nod1->dir_nod != 1) return -ENOENT;

	if(found(path2, &nod2) != 0) return -ENOENT;

	if(nod2->dir_nod != 1) return -ENOENT;

	char *old = nod1->contents;
	int tlen = userlen2 + 3;
	char *title = (char *) malloc(tlen + 1);
	title[0] = '[';
	for(int i = 0; i < userlen2; ++i) title[i + 1] = user2[i];
	title[tlen - 2] = ']';
	title[tlen - 1] = '\n';
	title[tlen] = '\0';

	strcat(debug, strdup(title));
	strcat(debug, "\n");

	int newsize = 0;
	if(offset + tlen + size > strlen(old)) newsize = offset + tlen + size;
	else newsize = strlen(old);

	char *newcts = (char *) malloc(newsize + 1);
	memcpy(newcts, old, strlen(old));
	memcpy(newcts + offset, title, tlen);
	memcpy(newcts + offset + tlen, buf, size);
	newcts[newsize] = '\0';

	nod1->contents = newcts;
	nod2->contents = strdup(newcts);
	
	return size;
}

static int chat_release(const char *path, struct fuse_file_info *fi)
{
	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;

	if(nod->dir_nod != 1) return -ENOENT;

	return 0;
}

static int chat_mknod(const char *path, mode_t mode, dev_t rdev)
{
	(void) rdev;
	(void) mode;

	int pathlen = strlen(path), namelen = 0;

	char name[maxlength_of_filename];
	
	int st = pathlen;
	while(st > 0 && path[st - 1] != '/') st--;
	if(st <= 0) return -ENOENT;
	while(st + namelen <= pathlen - 1) {
		name[namelen] = path[st + namelen];
		namelen++;
	}
	name[namelen] = '\0';
	
	int fpathlen = st;
	char *fpath = (char *) malloc(fpathlen + 1);
	for(int i = 0; i < st; ++i) fpath[i] = path[i];
	fpath[fpathlen] = '\0';

	struct node *fnod;
	if(found(fpath, &fnod) != 0) return -ENOENT;
	
	if(fnod->dir_nod != 2) return -ENOENT;

	struct node *nod = create(name, "", 1, NULL, NULL, NULL, NULL);
	add(fnod, nod);

	return 0;
}

static int chat_unlink(const char *path)
{
	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;
	
	if(nod->dir_nod != 1) return -ENOENT;

	del(nod);

	return 0;
}

static int chat_mkdir(const char *path, mode_t mode)
{
	(void) mode;

	int pathlen = strlen(path), namelen = 0;

	char name[maxlength_of_filename];
	
	int st = pathlen;
	while(st > 0 && path[st - 1] != '/') st--;
	if(st <= 0) return -1;
	while(st + namelen <= pathlen - 1) {
		name[namelen] = path[st + namelen];
		namelen++;
	}
	name[namelen] = '\0';
	
	int fpathlen = st;
	char *fpath = (char *) malloc(fpathlen + 1);
	for(int i = 0; i < st; ++i) fpath[i] = path[i];
	fpath[fpathlen] = '\0';

	struct node *fnod;
	if(found(fpath, &fnod) != 0) return -1;
	
	if(fnod->dir_nod != 2) return -3;

	struct node *nod = create(name, "", 2, NULL, NULL, NULL, NULL);
	add(fnod, nod);

	return 0;
}

static int chat_rmdir(const char *path)
{
	struct node *nod;
	if(found(path, &nod) != 0) return -ENOENT;

	if(nod->dir_nod != 2) return -ENOENT;

	del(nod);

	return 0;
}

static int chat_utimens(const char *path, const struct timespec tv[2],
			 struct fuse_file_info *fi)
{
    return 0;
}

static const struct fuse_operations chat_oper = {
	.init		= chat_init,
	.getattr	= chat_getattr,
	.readdir	= chat_readdir,
	.open		= chat_open,
	.read		= chat_read,
	.access		= chat_access,
	.write		= chat_write,
	.release	= chat_release,
	.mknod		= chat_mknod,
	.unlink		= chat_unlink,
	.mkdir		= chat_mkdir,
	.rmdir		= chat_rmdir,
	.utimens	= chat_utimens,
};

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	root = create("/", "", 2, NULL, NULL, NULL, NULL);

	debug = (char *) malloc(1500);
	debug[0] = '1';
	debug[1] = '\n';
	log = create("log", "", 1, NULL, NULL, NULL, NULL);
	log->contents = debug;
	add(root, log);

	ret = fuse_main(args.argc, args.argv, &chat_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
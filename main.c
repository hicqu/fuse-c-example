#define FUSE_USE_VERSION 26
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

static const char *realdir = "./realdir";

// A map from path to its undo_logs.
struct CMap;
extern struct CMap *init_hash_map();
extern void *hash_map_get(struct CMap *map, const char *key);
extern void free_map(struct CMap *map, void (*)(void *), void (*)(void *));
extern void *hash_map_insert(struct CMap *map, const char *key, void *value);
extern void *hash_map_remove(struct CMap *map, const char *key);

void *zero_malloc(size_t size) {
    void *ptr = malloc(size);
    bzero(ptr, size);
    return ptr;
}

struct undo_log_entry {
    off_t offset;
    char *ptr;
    size_t size;
    // The next entry in the same file.
    struct undo_log_entry *next;
    // The next entry in the global file system.
    struct undo_log_entry *global_next, *global_prev;
};

static struct undo_log_entry *undo_logs_head = NULL;
static struct undo_log_entry *undo_logs_tail = NULL;
static struct CMap *path_to_undo_logs = NULL;
static pthread_mutex_t undo_logs_mutex = PTHREAD_MUTEX_INITIALIZER;

static const size_t LARGE_UNDO_LOG_SISE = (1 << 20) * 32; // 32M 
static const int UNDO_LOG_FLUSH_INTERVAL = 30;             // 30s
static size_t undo_logs_size = 0;

// Must be called in lock context.
void free_undo_log(struct undo_log_entry *undo_log) {
    if (undo_log->ptr) {
        free(undo_log->ptr);
    }
    undo_logs_size -= undo_log->size;
    free(undo_log);
}

// Must be called in lock context.
void free_undo_logs_for_fsync(const char *path) {
    struct undo_log_entry *entry = hash_map_get(path_to_undo_logs, path);
    while (entry) {
        struct undo_log_entry *old_prev = entry->global_prev;
        struct undo_log_entry *old_next = entry->global_next;
        if (old_prev) {
            old_prev->global_next = old_next;
        } else {
            undo_logs_head = old_next;
        }
        if (old_next) {
            old_next->global_prev = old_prev;
        } else {
            undo_logs_tail = old_prev;
        }
        struct undo_log_entry *next = entry->next;
        free_undo_log(entry);
        entry = next;
    }
}

// Must be called in lock context.
void free_all_undo_logs() {
    struct undo_log_entry *entry = undo_logs_head;
    while (entry) {
        struct undo_log_entry *next = entry->global_next;
        free_undo_log(entry);
        entry = next;
    }
    free_map(path_to_undo_logs, free, NULL);
}

// get attributes from tmp file first, try real path if the tmp file doesn't exist.
static int getattr_callback(const char *path, struct stat *stbuf) {
    fprintf(stderr, "getattr %s\n", path);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    // Use lstat instead of stat.
    int error = lstat(real_path, stbuf);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int opendir_callback(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "opendir %s\n", path);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    DIR *dir = opendir(real_path);
    free(real_path);
    if (!dir) {
        return -errno;
    }
    fi->fh = (long)dir;
    return 0;
}

static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                            struct fuse_file_info *fi) {
    fprintf(stderr, "readdir %s, offset: %ld\n", path, offset);
    DIR *dir = (DIR *)fi->fh;
    struct dirent *ent = NULL;
    do {
        if (offset == (1UL << 63) - 1UL) {
            // It's an undocumented behavior. Be careful.
            return 0;
        }
        if (offset > 0) {
            seekdir(dir, offset);
        }

        errno = 0;
        ent = readdir(dir);
        if (!ent && errno != 0) {
            fprintf(stderr, "readdir fail: %s\n", strerror(errno));
            return errno;
        }
    } while (!ent);

    fprintf(stderr, "readdir get an entry %s\n", ent->d_name);
    char *ent_path = zero_malloc(1 + strlen(path) + 1 + strlen(ent->d_name));
    struct stat stbuf;
    int error = getattr_callback(ent_path, &stbuf);
    free(ent_path);
    if (error != 0) {
        return error;
    }

    return filler(buf, ent->d_name, &stbuf, telldir(dir));
}

static int open_callback(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "open %s, flags: %o, fi ptr: %lx\n", path, fi->flags, (long)fi);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);
    fi->fh = open(real_path, fi->flags);
    free(real_path);
    return 0;
}

static int read_callback(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {
    fprintf(stderr, "read %s from %ld, want %ld bytes, fi ptr: %lx\n", path, offset, size,
            (long)fi);
    ssize_t readed = pread(fi->fh, buf, size, offset);
    if (readed < 0) {
        return -errno;
    }
    return readed;
}

static int trunc_callback(const char *path, off_t offset) {
    fprintf(stderr, "truncate %s to %ld\n", path, offset);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = truncate(real_path, offset);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int readlink_callback(const char *path, char *buf, size_t size) {
    fprintf(stderr, "readlink %s, want %ld bytes\n", path, size);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int bytes = readlink(real_path, buf, size);
    free(real_path);
    if (bytes == -1) {
        return -errno;
    } else {
        return 0;
    }
}

static int mkdir_callback(const char *path, mode_t mode) {
    fprintf(stderr, "mkdir %s with mode %o\n", path, mode);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = mkdir(real_path, mode | S_IFDIR);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int unlink_callback(const char *path) {
    fprintf(stderr, "unlink %s\n", path);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = unlink(real_path);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int rmdir_callback(const char *path) {
    fprintf(stderr, "rmdir %s\n", path);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = rmdir(real_path);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int do_rename(const char *path1, const char *path2) {
    char *real_path1 = zero_malloc(1 + strlen(path1) + strlen(realdir));
    sprintf(real_path1, "%s%s", realdir, path1);
    char *real_path2 = zero_malloc(1 + strlen(path2) + strlen(realdir));
    sprintf(real_path2, "%s%s", realdir, path2);

    int error = rename(real_path1, real_path2);
    free(real_path1);
    free(real_path2);
    if (error) {
        return -errno;
    }
    return 0;
}

static int rename_callback(const char *path1, const char *path2) {
    fprintf(stderr, "rename %s to %s\n", path1, path2);
    int error = do_rename(path1, path2);
    if (error < 0) {
        return error;
    }

    pthread_mutex_lock(&undo_logs_mutex);
    void *undo_log = hash_map_remove(path_to_undo_logs, path1);
    if (undo_log) {
        hash_map_insert(path_to_undo_logs, path2, undo_log);
    }
    pthread_mutex_unlock(&undo_logs_mutex);
    return 0;
}

static int mknod_callback(const char *path, mode_t mode, dev_t dev) {
    fprintf(stderr, "mknod %s with mode %d\n", path, mode);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = mknod(real_path, mode, dev);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int create_callback(const char *path, mode_t mode, struct fuse_file_info *fi) {
    fprintf(stderr, "create %s with mode %d\n", path, mode);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    fi->flags = O_CREAT;
    fi->fh = open(real_path, fi->flags, mode);
    free(real_path);
    if (fi->fh < 0) {
        return -errno;
    }
    return 0;
}

static int utimens_callback(const char *path, const struct timespec tv[2]) {
    fprintf(stderr, "utimens %s\n", path);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = utimensat(AT_FDCWD, real_path, tv, 0);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int chmod_callback(const char *path, mode_t mode) {
    fprintf(stderr, "chmod %s to mode %d\n", path, mode);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = chmod(real_path, mode);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int chown_callback(const char *path, uid_t uid, gid_t gid) {
    fprintf(stderr, "chown%s to %d:%d\n", path, uid, gid);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = chown(real_path, uid, gid);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

// NOTE: flush will be called after a file descriptor is closed.
static int flush_callback(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "flush %s, close the file descriptor\n", path);
    fi->fh = 0; // It's ok because the file descriptor is closed in `release`.
    fi->flush = 1;
    return 0;
}

static int link_callback(const char *path, const char *link_path) {
    fprintf(stderr, "link %s to %s\n", path, link_path);

    char *real_path1 = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path1, "%s%s", realdir, path);
    char *real_path2 = zero_malloc(1 + strlen(realdir) + strlen(link_path));
    sprintf(real_path2, "%s%s", realdir, link_path);

    int error = link(real_path1, real_path2);
    free(real_path1);
    free(real_path2);
    if (error) {
        return -errno;
    }
    return 0;
}

static int symlink_callback(const char *path, const char *link_path) {
    fprintf(stderr, "symlink %s to %s\n", path, link_path);

    char *real_path1 = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path1, "%s%s", realdir, path);
    char *real_path2 = zero_malloc(1 + strlen(realdir) + strlen(link_path));
    sprintf(real_path2, "%s%s", realdir, link_path);

    int error = symlink(real_path1, real_path2);
    free(real_path1);
    free(real_path2);
    if (error) {
        return -errno;
    }
    return 0;
}

static int write_callback(const char *path, const char *buf, size_t size, off_t offset,
                          struct fuse_file_info *fi) {
    fprintf(stderr, "write %s from %ld, want %ld bytes, fi ptr: %lx\n", path, offset, size,
            (long)fi);
    assert(!fi->flush);

    // Allocate an undo log, and set its offset and path.
    struct undo_log_entry *undo_log = zero_malloc(sizeof(struct undo_log_entry));
    undo_log->offset = offset;

    // Set its ptr and size.
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);
    int read_fd = open(real_path, O_RDONLY);
    assert(read_fd >= 0);
    undo_log->ptr = zero_malloc(size);
    ssize_t backup_size = pread(read_fd, undo_log->ptr, size, offset);
    assert(backup_size >= 0);

    if (backup_size == 0) {
        free(undo_log->ptr);
        undo_log->ptr = NULL;
    } else {
        undo_log->size = backup_size;
    }

    pthread_mutex_lock(&undo_logs_mutex);
    // Link the undo log to the global buffer.
    if (!undo_logs_tail) {
        undo_logs_tail = undo_log;
        undo_logs_head = undo_log;
    } else {
        undo_log->global_prev = undo_logs_tail;
        undo_logs_tail->global_next = undo_log;
        undo_logs_tail = undo_log;
    }
    // Link the undo log to the file's buffer.
    undo_log->next = hash_map_get(path_to_undo_logs, path);
    hash_map_insert(path_to_undo_logs, path, undo_log);
    undo_logs_size += undo_log->size;
    pthread_mutex_unlock(&undo_logs_mutex);

    ssize_t writed = pwrite(fi->fh, buf, size, offset);
    if (writed < 0) {
        return -errno;
    }
    return writed;
}

// it's called when all file descriptors are closed.
static int release_callback(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "release %s\n", path);
    if (close(fi->fh)) {
        return -errno;
    }
    fi->fh = 0;
    return 0;
}

static int fsync_callback(const char *path, int datasync /* only sync user data */,
                          struct fuse_file_info *fi) {
    fprintf(stderr, "fsync %s, only care about data: %d\n", path, datasync);
    pthread_mutex_lock(&undo_logs_mutex);
    free_undo_logs_for_fsync(path);
    pthread_mutex_unlock(&undo_logs_mutex);
    return 0;
}

static int getxattr_callback(const char *path, const char *name, char *value, size_t size) {
    fprintf(stderr, "getxattr %s, %s -> %s\n", path, name, value);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = getxattr(path, name, value, size);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int setxattr_callback(const char *path, const char *name, const char *value, size_t size,
                             int flags) {
    fprintf(stderr, "setxattr %s, %s -> %s\n", path, name, value);
    char *real_path = zero_malloc(1 + strlen(realdir) + strlen(path));
    sprintf(real_path, "%s%s", realdir, path);

    int error = setxattr(path, name, value, size, flags);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static struct fuse_operations fuse_example_operations = {
    .getattr = getattr_callback,
    .opendir = opendir_callback,
    .readdir = readdir_callback,
    .open = open_callback,
    .read = read_callback,
    .truncate = trunc_callback,
    .readlink = readlink_callback,
    .mkdir = mkdir_callback,
    .unlink = unlink_callback,
    .rmdir = rmdir_callback,
    .rename = rename_callback,
    .create = create_callback,
    .link = link_callback,
    .symlink = symlink_callback,
    .utimens = utimens_callback,
    .chmod = chmod_callback,
    .chown = chown_callback,
    .flush = flush_callback,
    .mknod = mknod_callback,
    .write = write_callback,
    .release = release_callback,
    .fsync = fsync_callback,
    .getxattr = getxattr_callback,
    .setxattr = setxattr_callback,
};

void *fs_event_loop(void *arg) {
    struct fuse *fs = (struct fuse *)arg;
    fuse_loop(fs);
    return NULL;
}

void *flush_loop(void *arg) {
    time_t start = time(NULL);
    while (1) {
        sleep(1);
        time_t now = time(NULL);
        if (now - start < UNDO_LOG_FLUSH_INTERVAL) {
            continue;
        }
        if (__sync_fetch_and_add(&undo_logs_size, 0) < LARGE_UNDO_LOG_SISE) {
            continue;
        }
        start = now;
        pthread_mutex_lock(&undo_logs_mutex);
        free_all_undo_logs();
        pthread_mutex_unlock(&undo_logs_mutex);
    }
}

int main(int argc, char *argv[]) {
    char *mountpoint;
    int multithreaded;
    struct fuse *fs = fuse_setup(argc, argv, &fuse_example_operations,
                                 sizeof(struct fuse_operations), &mountpoint, &multithreaded, NULL);
    pthread_t fs_thread, flush_thread;
    pthread_create(&fs_thread, NULL, fs_event_loop, fs);
    pthread_create(&flush_thread, NULL, flush_loop, NULL);

    sleep(30000000);
    fuse_teardown(fs, mountpoint);
    return 0;
}

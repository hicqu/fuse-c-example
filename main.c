#define FUSE_USE_VERSION 26
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

static const char *realdir = "./realdir";

static int getattr_callback(const char *path, struct stat *stbuf) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
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
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
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
    if (offset == (1UL << 63) - 1UL) {
        return 0;
    }
    DIR *dir = (DIR *)fi->fh;
    if (offset > 0) {
        seekdir(dir, offset);
    }

    errno = 0;
    struct dirent *ent = readdir(dir);
    if (!ent && errno != 0) {
        return errno;
    }

    struct stat stbuf;
    char *real_path = malloc(1 + strlen(path) + strlen(realdir) + 1 + strlen(ent->d_name));
    sprintf(real_path, "%s%s/%s", realdir, path, ent->d_name);

    // Use lstat instead of stat.
    int error = lstat(real_path, &stbuf);
    free(real_path);
    if (error) {
        return -errno;
    }
    return filler(buf, ent->d_name, &stbuf, telldir(dir));
}

static int open_callback(const char *path, struct fuse_file_info *fi) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    fi->fh = open(real_path, fi->flags);
    free(real_path);
    return 0;
}

static int read_callback(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {
    assert(lseek(fi->fh, offset, SEEK_SET) >= 0);
    ssize_t readed = read(fi->fh, buf, size);
    if (readed < 0) {
        return -errno;
    }
    return readed;
}

static int trunc_callback(const char *path, off_t offset) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = truncate(real_path, offset);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int readlink_callback(const char *path, char *buf, size_t size) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
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
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = mkdir(real_path, mode | S_IFDIR);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int unlink_callback(const char *path) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = unlink(real_path);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int rmdir_callback(const char *path) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = rmdir(real_path);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int rename_callback(const char *path1, const char *path2) {
    size_t path_len = strlen(path1);
    char *real_path1 = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path1, "%s%s", realdir, path1);

    path_len = strlen(path2);
    char *real_path2 = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path2, "%s%s", realdir, path2);

    int error = rename(real_path1, real_path2);
    free(real_path1);
    free(real_path2);
    if (error) {
        return -errno;
    }
    return 0;
}

static int mknod_callback(const char *path, mode_t mode, dev_t dev) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = mknod(real_path, mode, dev);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int create_callback(const char *path, mode_t mode, struct fuse_file_info *fi) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    fi->flags = O_CREAT | O_RDWR;
    fi->fh = open(real_path, fi->flags, mode);
    free(real_path);
    if (fi->fh < 0) {
        return -errno;
    }
    return 0;
}

static int utimens_callback(const char *path, const struct timespec tv[2]) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = utimensat(AT_FDCWD, real_path, tv, 0);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int chmod_callback(const char *path, mode_t mode) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = chmod(real_path, mode);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int chown_callback(const char *path, uid_t uid, gid_t gid) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path, "%s%s", realdir, path);

    int error = chown(real_path, uid, gid);
    free(real_path);
    if (error) {
        return -errno;
    }
    return 0;
}

static int flush_callback(const char *path, struct fuse_file_info *fi) {
    fi->flush = 1;
    return 0;
}

static int link_callback(const char *path, const char *link_path) {
    size_t path_len = strlen(path);
    char *real_path1 = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path1, "%s%s", realdir, path);

    path_len = strlen(link_path);
    char *real_path2 = malloc(1 + path_len + strlen(realdir));
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
    size_t path_len = strlen(path);
    char *real_path1 = malloc(1 + path_len + strlen(realdir));
    sprintf(real_path1, "%s%s", realdir, path);

    path_len = strlen(link_path);
    char *real_path2 = malloc(1 + path_len + strlen(realdir));
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
    assert(fi->fh > 0);
    assert(lseek(fi->fh, offset, SEEK_SET) >= 0);
    ssize_t writed = write(fi->fh, buf, size);
    if (writed < 0) {
        return -errno;
    }
    return writed;
}

static int release_callback(const char *path, struct fuse_file_info *fi) {
    assert(fi->fh >= 0);
    if (close(fi->fh)) {
        return -errno;
    }
    fi->fh = 0;
    return 0;
}

static int fsync_callback(const char *path, int datasync /* only sync user data */,
                          struct fuse_file_info *fi) {
    return 0;
}

static int setxattr_callback(const char *path, const char *name, const char *value, size_t size,
                             int flags) {
    size_t path_len = strlen(path);
    char *real_path = malloc(1 + path_len + strlen(realdir));
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
    .setxattr = setxattr_callback,
};

int main(int argc, char *argv[]) { return fuse_main(argc, argv, &fuse_example_operations, NULL); }

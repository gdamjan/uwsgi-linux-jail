#define _GNU_SOURCE
#include <uwsgi.h>
extern struct uwsgi_server uwsgi;

// forward declarations
static void mount_proc();
static void create_dev ();
static void create_private_fs ();
static void unmount_recursive(const char *dir);
static void map_id(const char *, uint32_t, uint32_t);

static void jail() {
    uid_t real_euid = geteuid();
    gid_t real_egid = getegid();

    //int unshare_flags;
    //unshare_flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWUSER;

    if (-1 == unshare(CLONE_NEWUSER)) {
        uwsgi_error("unshare(CLONE_NEWUSER) failed");
        exit(1);
    }

    if (-1 == unshare(CLONE_NEWIPC)) {
        uwsgi_error("unshare(CLONE_NEWIPC) failed");
        exit(1);
    }

    if (-1 == unshare(CLONE_NEWUTS)) {
        uwsgi_error("unshare(CLONE_NEWUTS) failed");
        exit(1);
    }

    if (-1 == unshare(CLONE_NEWNS)) {
        uwsgi_error("unshare(CLONE_NEWNS) failed");
        exit(1);
    }

    create_private_fs();

    map_id("/proc/self/uid_map", 0, real_euid);
    map_id("/proc/self/gid_map", 0, real_egid);
}

static void create_private_fs () {
    char newroot[] = "/tmp/nsroot-XXXXXX";
    if (NULL == mkdtemp(newroot)) {
       uwsgi_error("mkdtemp");
       exit(1);
    };

    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    mount(NULL, newroot, "tmpfs", 0, NULL);

    char *orig_root = uwsgi_concat2(newroot, "/.orig_root");
    mkdir(orig_root, 0755);
    pivot_root(newroot, orig_root);
    if (chdir("/") != 0) {
        uwsgi_error("chdir(/)");
        exit(1);
    }

    create_dev();
    mount_proc();

    mkdir("/usr", 0755);
    char *orig_usr = uwsgi_concat2(orig_root, "/usr");
    mount(orig_usr, "/usr", "none", MS_BIND | MS_RDONLY, NULL);
    // mount(NULL, "/usr", NULL, MS_RDONLY | MS_REMOUNT, NULL);
    free(orig_usr);

    char *orig_newroot = uwsgi_concat2(orig_root, newroot);
    rmdir(orig_newroot);
    free(orig_newroot);

    unmount_recursive(orig_root);

    free(orig_root);
}

static void unmount_recursive(const char *dir) {
    // TODO: uwsgi or util-linux solution?
    umount(dir);
}

static void create_dev () {
    /* create a minimal /dev structure */
    dev_t dev;

    if (mkdir("/dev", 0755) != 0) {
        uwsgi_error("mkdir(/dev)");
        exit(1);
    }

    dev = makedev(1, 3);
    mknod("/dev/null", 0666 & S_IFCHR, dev);

    dev = makedev(1, 5);
    mknod("/dev/zero", 0666 & S_IFCHR, dev);

    dev = makedev(1, 7);
    mknod("/dev/full", 0666 & S_IFCHR, dev);

    dev = makedev(1, 8);
    mknod("/dev/random", 0666 & S_IFCHR, dev);

    dev = makedev(1, 9);
    mknod("/dev/urandom", 0666 & S_IFCHR, dev);

    if (symlink("/proc/self/fd", "/dev/fd") != 0) {
        uwsgi_error("symlink(/proc/self/fd)");
        exit(1);
    }
    if (symlink("/proc/self/fd/0", "/dev/stdin") != 0) {
        uwsgi_error("symlink(/proc/self/fd/0)");
        exit(1);
    }
    if (symlink("/proc/self/fd/1", "/dev/stdout") != 0) {
        uwsgi_error("symlink(/proc/self/fd/1)");
        exit(1);
    }
    if (symlink("/proc/self/fd/2", "/dev/stderr") != 0) {
        uwsgi_error("symlink(/proc/self/fd/2)");
        exit(1);
    }
}

static void mount_proc() {
    mkdir("/proc", 0555);
    mount("proc", "/proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL);
}

/* from util-linux unshare.c */
static void map_id(const char *file, uint32_t from, uint32_t to) {
    char buf[1024];
    int fd;

    fd = open(file, O_WRONLY);
    if (fd < 0) {
        uwsgi_error_open(file);
        exit(1);
    }

    sprintf(buf, "%u %u 1", from, to);
    if (write(fd, buf, strlen(buf))) {
        uwsgi_log("write to %s failed: %s [%s line %d]\n", file, strerror(errno), __FILE__, __LINE__);
        exit(1);
    }
    close(fd);
}

// static struct uwsgi_option uwsgi_linuxjail_options[] = {}

struct uwsgi_plugin linuxjail_plugin = {
    .jail = jail,
    .name = "linuxjail"
};

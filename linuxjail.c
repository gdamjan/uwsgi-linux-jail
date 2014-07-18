#define _GNU_SOURCE
#include <uwsgi.h>
extern struct uwsgi_server uwsgi;

// forward declarations
static void create_dev ();
static void unmount_recursive(char *dir);
static void map_id(const char *, uint32_t, uint32_t);

static void do_the_jail() {
    uid_t real_euid = geteuid();
    gid_t real_egid = getegid();

    //int unshare_flags;
    //unshare_flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWUSER;

    if (-1 == unshare(CLONE_NEWUSER)) {
        uwsgi_fatal_error("unshare(CLONE_NEWUSER) failed");
    }
    if (-1 == unshare(CLONE_NEWNS)) {
        uwsgi_fatal_error("unshare(CLONE_NEWNS) failed");
    }
    if (-1 == unshare(CLONE_NEWIPC)) {
        uwsgi_fatal_error("unshare(CLONE_NEWIPC) failed");
    }
    if (-1 == unshare(CLONE_NEWUTS)) {
        uwsgi_fatal_error("unshare(CLONE_NEWUTS) failed");
    }

    map_id("/proc/self/uid_map", 0, real_euid);
    map_id("/proc/self/gid_map", 0, real_egid);

    char newroot[] = "/tmp/nsroot-XXXXXX";
    if (NULL == mkdtemp(newroot)) {
       uwsgi_fatal_error("mkdtemp");
    };

    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    mount(NULL, newroot, "tmpfs", 0, NULL);

    char *orig_root = uwsgi_concat2(newroot, "/.orig_root");
    if (mkdir(orig_root, 0755) != 0) {
        uwsgi_fatal_error("mkdir(orig_root)");
    }
    if (pivot_root(newroot, orig_root) != 0) {
        uwsgi_fatal_error("pivot_root");
    }

    if (chdir("/") != 0) {
        uwsgi_fatal_error("chdir(/)");
    }

    create_dev();

    if (mkdir("/usr", 0755) != 0) {
        uwsgi_fatal_error("mkdir(/usr)");
    };
    if (mount("/.orig_root/usr", "/usr", "none", MS_BIND, NULL) != 0) {
        uwsgi_fatal_error("mount(/usr)");
    }
    if (mount(NULL, "/usr", NULL, MS_RDONLY | MS_REMOUNT, NULL) != 0) {
        uwsgi_fatal_error("remount(/usr)");
    }

    char *orig_newroot = uwsgi_concat2("/.orig_root", newroot);
    rmdir(orig_newroot);
    free(orig_newroot);

    unmount_recursive("/.orig_root");

    if (mkdir("/proc", 0555) != 0) {
        uwsgi_fatal_error("mkdir(/proc)");
    }
    if (mount("proc", "/proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL) != 0) {
        uwsgi_fatal_error("mount(/proc)");
    }

    free(orig_root);
}

static void unmount_recursive(char *dir) {
    FILE *procmounts;
    char line[1024];
    int unmounted = -1;

    char *self_mounts = uwsgi_concat2(dir, "/proc/self/mounts");

    // try until there's nothing to unmount
    while (unmounted != 0) {
        unmounted = 0;
        procmounts = fopen(self_mounts, "r");
        if (procmounts == NULL) {
            uwsgi_fatal_error("fopen(/proc/self/mounts)");
        }

        char *delim0, *delim1;
        while (fgets(line, 1024, procmounts) != NULL) {
            delim0 = strchr(line, ' ');
            if (delim0 == NULL) continue;
            delim0++;
            delim1 = strchr(delim0, ' ');
            if (delim1 == NULL) continue;
            *delim1 = 0;

            // unmount just the filesystems under dir
            if (strstr(delim0, dir) == delim0) {
                printf("Unmounting %s\n", delim0);
                umount(delim0);
                unmounted++;
            }
        }
        fclose(procmounts);
    }
    free(self_mounts);
}

static void create_dev () {
    /* create a minimal /dev structure */
    dev_t dev;

    if (mkdir("/dev", 0755) != 0) {
        uwsgi_fatal_error("mkdir(/dev)");
    }

    dev = makedev(1, 3);
    if (mknod("/dev/null", 0666 & S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/null)");
    }

    dev = makedev(1, 5);
    if (mknod("/dev/zero", 0666 & S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/zero)");
    }

    dev = makedev(1, 7);
    if (mknod("/dev/full", 0666 & S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/full)");
    }

    dev = makedev(1, 8);
    if (mknod("/dev/random", 0666 & S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/random)");
    }

    dev = makedev(1, 9);
    if (mknod("/dev/urandom", 0666 & S_IFCHR, dev) !=0) {
        uwsgi_fatal_error("mknod(/dev/urandom)");
    }

    if (symlink("/proc/self/fd", "/dev/fd") != 0) {
        uwsgi_fatal_error("symlink(/proc/self/fd)");
    }
    if (symlink("/proc/self/fd/0", "/dev/stdin") != 0) {
        uwsgi_fatal_error("symlink(/proc/self/fd/0)");
    }
    if (symlink("/proc/self/fd/1", "/dev/stdout") != 0) {
        uwsgi_fatal_error("symlink(/proc/self/fd/1)");
    }
    if (symlink("/proc/self/fd/2", "/dev/stderr") != 0) {
        uwsgi_fatal_error("symlink(/proc/self/fd/2)");
    }
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
    if (write(fd, buf, strlen(buf)) < 0) {
        uwsgi_log("write to %s failed: %s [%s line %d]\n", file, strerror(errno), __FILE__, __LINE__);
        exit(1);
    }
    close(fd);
}

// static struct uwsgi_option uwsgi_linuxjail_options[] = {}

struct uwsgi_plugin linuxjail_plugin = {
    .jail = do_the_jail,
    .name = "linuxjail"
};

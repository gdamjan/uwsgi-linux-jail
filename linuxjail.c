#define _GNU_SOURCE
#include <uwsgi.h>
extern struct uwsgi_server uwsgi;

#define ORIG_ROOT  "/.orig_root"
#define TEMP_ROOT  "/tmp/nsroot-XXXXXX"

// forward declarations
static void create_dev ();
static int mount_proc ();
static void map_id(const char *, uint32_t, uint32_t);

static void do_the_jail() {
    uid_t real_euid = geteuid();
    gid_t real_egid = getegid();

    int unshare_flags;
    unshare_flags = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWIPC;  // CLONE_NEWNET

    if (unshare(unshare_flags) != 0) {
        uwsgi_fatal_error("unshare failed");
    }

    map_id("/proc/self/uid_map", 0, real_euid);
    map_id("/proc/self/gid_map", 0, real_egid);

    char newroot[] = TEMP_ROOT;
    if (mkdtemp(newroot) == NULL) {
       uwsgi_fatal_error("mkdtemp");
    };

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        uwsgi_fatal_error("remount / as private");
    }
    if (mount(NULL, newroot, "tmpfs", 0, NULL) != 0) {
        uwsgi_fatal_error("mount(/, tmpfs)");
    }

    char *orig_root = uwsgi_concat2(newroot, ORIG_ROOT);
    if (mkdir(orig_root, 0755) != 0) {
        uwsgi_fatal_error("mkdir(orig_root)");
    }
    if (pivot_root(newroot, orig_root) != 0) {
        uwsgi_fatal_error("pivot_root");
    }

    if (chdir("/") != 0) {
        uwsgi_fatal_error("chdir(/)");
    }

    char *orig_newroot = uwsgi_concat2(ORIG_ROOT, newroot);
    if (rmdir(orig_newroot) != 0) {
        uwsgi_error("rmdir temp root dir");
    }
    free(orig_newroot);

    create_dev();

    if (mkdir("/usr", 0755) != 0 ||
        mount(ORIG_ROOT "/usr", "/usr", "none", MS_BIND, NULL) != 0 ||
        mount(NULL, "/usr", NULL, MS_BIND | MS_RDONLY | MS_REMOUNT, NULL) != 0) {
            uwsgi_fatal_error("Read-only bind mount /usr");
    }
    if (mkdir("/bin", 0755) != 0 ||
        mount(ORIG_ROOT "/bin", "/bin", "none", MS_BIND, NULL) != 0 ||
        mount(NULL, "/bin", NULL, MS_BIND | MS_RDONLY | MS_REMOUNT, NULL) != 0) {
            uwsgi_fatal_error("Read-only bind mount /bin");
    }
    if (mkdir("/sbin", 0755) != 0 ||
        mount(ORIG_ROOT "/sbin", "/sbin", "none", MS_BIND, NULL) != 0 ||
        mount(NULL, "/sbin", NULL, MS_BIND | MS_RDONLY | MS_REMOUNT, NULL) != 0) {
            uwsgi_fatal_error("Read-only bind mount /sbin");
    }
    if (mkdir("/lib", 0755) != 0 ||
        mount(ORIG_ROOT "/lib", "/lib", "none", MS_BIND, NULL) != 0 ||
        mount(NULL, "/lib", NULL, MS_BIND | MS_RDONLY | MS_REMOUNT, NULL) != 0) {
            uwsgi_fatal_error("Read-only bind mount /lib");
    }
    if (mkdir("/lib64", 0755) != 0 ||
        mount(ORIG_ROOT "/lib64", "/lib64", "none", MS_BIND, NULL) != 0 ||
        mount(NULL, "/lib64", NULL, MS_BIND | MS_RDONLY | MS_REMOUNT, NULL) != 0) {
            uwsgi_fatal_error("Read-only bind mount /lib64");
    }

    if (umount2(ORIG_ROOT, MNT_DETACH) != 0) {
        uwsgi_fatal_error("unmount all in old root");
    }
    if (rmdir(ORIG_ROOT) != 0) {
        uwsgi_error("rmdir " ORIG_ROOT);
    }

    if (mount_proc() != 0) {
        exit(EXIT_FAILURE);
    }

    // Debug only, to find out why the above mount /proc is not working
    printf("eUID = %ld;  eGID = %ld;  ",
          (long) geteuid(), (long) getegid());

    cap_t caps = cap_get_proc();
    printf("capabilities: %s\n", cap_to_text(caps, NULL));
    if (system("/bin/bash") != 0) {
        uwsgi_fatal_error("system");
    }


    free(orig_root);
}

static int mount_proc () {
    int pid, status;

    pid = fork();
    if (pid == 0) {
        if (mkdir("/proc", 0555) != 0) {
            uwsgi_fatal_error("mkdir(/proc)");
        }
        if (mount("proc", "/proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL) != 0) {
            uwsgi_fatal_error("mount(/proc)");
        }
        exit(0);
    }
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

static void create_dev () {
    /* create a minimal /dev structure */
    dev_t dev;

    if (mkdir("/dev", 0755) != 0) {
        uwsgi_fatal_error("mkdir(/dev)");
    }

    dev = makedev(1, 3);
    if (mknod("/dev/null", 0666 | S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/null)");
    }

    dev = makedev(1, 5);
    if (mknod("/dev/zero", 0666 | S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/zero)");
    }

    dev = makedev(1, 7);
    if (mknod("/dev/full", 0666 | S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/full)");
    }

    dev = makedev(1, 8);
    if (mknod("/dev/random", 0666 | S_IFCHR, dev) != 0) {
        uwsgi_fatal_error("mknod(/dev/random)");
    }

    dev = makedev(1, 9);
    if (mknod("/dev/urandom", 0666 | S_IFCHR, dev) !=0) {
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
        exit(EXIT_FAILURE);
    }

    sprintf(buf, "%u %u 1", from, to);
    if (write(fd, buf, strlen(buf)) < 0) {
        uwsgi_log("write to %s failed: %s [%s line %d]\n", file, strerror(errno), __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

// static struct uwsgi_option uwsgi_linuxjail_options[] = {}

struct uwsgi_plugin linuxjail_plugin = {
    .jail = do_the_jail,
    .name = "linuxjail"
};

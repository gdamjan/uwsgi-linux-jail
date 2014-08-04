=============================
A custom Linux jail for uwsgi
=============================

A custom jail/namespace plugin for uwsgi, requires Linux kernel >= 3.8 and CONFIG_USER_NS enabled in the kernel (as well
as the other CONFIG_*_NS options). It's hardcoded to be practical to my own use case.


Features
========
 - no options, compile time configuration
   - except app dir, mounted on /app
   - except static dir, mounted on /static, perhaps http published
   - optional writable dir?? better not :)
 - namespaces: mount, pid, ipc, net?, uts, user
 - drop all capabilities (except CAP_NET_ADMIN perhaps?)
 - / is a tmpfs
 - /usr is a read-only bind mount of system /usr
 - /proc, /dev (zero, urandom, null, random), /sys?, /dev/shm?
 - /var, /tmp (just mkdir them?)
 - /bin, /sbin, /lib, /lib64 are links to `/usr/*`
 - chdir /


Issues
======
 - How to have /dev in a user namespace (not possible on tmpfs?)


Build
=====

    uwsgi --build-plugin https://github.com/gdamjan/uwsgi-linux-jail

or if you're in the source dir:

    uwsgi --build-plugin .


References
==========

 - http://lwn.net/Articles/531114/
 - http://lwn.net/Kernel/Index/#Namespaces
 - http://uwsgi-docs.readthedocs.org/en/latest/Hooks.html
 - man 2 unshare
 - man 2 clone
 - man 2 setns
 - man 2 pivot_root

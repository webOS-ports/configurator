// A file created with open(O_CREAT) and no O_NOFOLLOW is written through a
// symlink planted where it goes. The stamps are created in a cache
// directory; one planted there pointed the configurator at any file root can
// write. Add O_NOFOLLOW (and O_CLOEXEC).
@safe@
expression P;
position p;
@@
open@p(P, <+... O_NOFOLLOW ...+>, ...)

@@
expression P;
position p != safe.p;
@@
* open@p(P, <+... O_CREAT ...+>, ...)

// A break inside a readdir() loop ends the scan of the directory: every
// entry after the one that failed is left out, in whatever order readdir()
// returns them. A dangling symlink took out the files after it this way.
// Log the entry and continue.
@@
expression D;
@@
readdir(D)
... when any
* break;

/*
 * Package btree routines.
 */

#include "main-menu.h"

#include <search.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void *root=NULL;

int _tree_compare (const void *p1, const void *p2) {
	return strcmp(((struct package_t *)p1)->package,
		      ((struct package_t *)p2)->package);
}

/* Find a package. */
struct package_t *tree_find(char *packagename) {
	struct package_t find;
	void *ret;

	find.package = packagename;
	ret=tfind(&find, &root, _tree_compare);
	if (ret)
		return *(struct package_t **)ret;
	return NULL;
}

/* 
 * Adds a package to the tree. Just feed in the package's name; the
 * struct to hold it is automatically created.
 */
struct package_t *tree_add(const char *packagename) {
	struct package_t *pkg=malloc(sizeof(struct package_t));
	memset(pkg, 0, sizeof(struct package_t));
	pkg->package = strdup(packagename);
	/* 
	 * I should just be able to return tsearch's return code, but this
	 * makes my code fail horribly later on. I think tsearch has
	 * issues.
	 */
	tsearch(pkg, &root, _tree_compare);
	return pkg;
}

#if DODEBUG
void _tree_dump(const void *nodep, const VISIT which, const int depth) {
	int i;
        struct package_t *p = *(struct package_t **)nodep;

	for (i=0; i < depth; i++)
		printf("    ");
	
        switch(which) {
                case postorder:
                        printf("    at"); /* false indent */
                        break;
                case preorder:
                        printf("begin");
                        break;
                case endorder:
                        printf("end");
                        break;
                case leaf:
                        printf("leaf");
                        break;
        }

	printf(" %s\n", p->package);
}

/* Dump out the tree. */
void tree_dump() {
        twalk(root, _tree_dump);
}
#endif

void _tree_free(void *nodep) {
	struct package_t *p = (struct package_t **)nodep;
	
	if (p->description)
		free(p->description);
	if (p->package)
		free(p->package);
	free(p);
}

/* Clears out the entire tree, freeing all package stucts contained it in. */
void tree_clear() {
	/*
	 * The correct way to do this is explained in the tsearch() man
	 * page in the example. Unfortunatly, glibc is broken, and using a
	 * twalk() with nested tdelete() calls will not delete the whole
	 * tree (see Debian bug #63575).
	 *
	 * So what I do instead is use glibc's tdestroy function. 
	 * WARNING: probably not portable!
	 */
	tdestroy(root, _tree_free);
	root = NULL;
}

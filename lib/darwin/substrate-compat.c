#include "substitute.h"
#include "substitute-internal.h"
#include <syslog.h>
#include <mach-o/dyld.h>

EXPORT
void *SubGetImageByName(const char *filename) __asm__("SubGetImageByName");
void *SubGetImageByName(const char *filename) {
    return substitute_open_image(filename);
}

EXPORT
void *SubFindSymbol(void *image, const char *name) __asm__("SubFindSymbol");
void *SubFindSymbol(void *image, const char *name) {
    if (!image) {
        const char *s = "SubFindSymbol: 'any image' specified, which is incredibly slow - like, 2ms on a fast x86.  I'm going to do it since it seems to be somewhat common, but you should be ashamed of yourself.";
        syslog(LOG_WARNING, "%s", s);
        fprintf(stderr, "%s\n", s);
        /* and it isn't thread safe, but neither is MS */
        for(uint32_t i = 0; i < _dyld_image_count(); i++) {
            const char *im_name = _dyld_get_image_name(i);
            struct substitute_image *im = substitute_open_image(im_name);
            if (!im) {
                fprintf(stderr, "(btw, couldn't open %s?)\n", im_name);
                continue;
            }
            void *r = SubFindSymbol(im, name);
            substitute_close_image(im);
            if (r)
                return r;
        }
        return NULL;
    }

    void *ptr;
    if (substitute_find_private_syms(image, &name, &ptr, 1))
        return NULL;
    return ptr;
}

#ifdef TARGET_DIS_SUPPORTED
EXPORT
void SubHookFunction(void *symbol, void *replace, void **result)
    __asm__("SubHookFunction");
void SubHookFunction(void *symbol, void *replace, void **result) {
    struct substitute_function_hook hook = {symbol, replace, result};
    int ret = substitute_hook_functions(&hook, 1, NULL,
                                        SUBSTITUTE_NO_THREAD_SAFETY);
    if (ret) {
        substitute_panic("SubHookFunction: substitute_hook_functions returned %s\n",
                         substitute_strerror(ret));
    }
}
#endif

EXPORT
void SubHookMessageEx(Class _class, SEL sel, IMP imp, IMP *result)
    __asm__("SubHookMessageEx");

void SubHookMessageEx(Class _class, SEL sel, IMP imp, IMP *result) {
    int ret = substitute_hook_objc_message(_class, sel, imp, result, NULL);
    if (ret) {
        substitute_panic("SubHookMessageEx: substitute_hook_objc_message returned %s\n",
                         substitute_strerror(ret));
    }
}

EXPORT
void *MSGetImageByName(const char *filename) {
    return SubGetImageByName(filename);
}

EXPORT
void *MSFindSymbol(void *image, const char *name) {
	return SubFindSymbol(image, name);
}

EXPORT
void MSHookFunction(void *symbol, void *replace, void **result) {
	SubHookFunction(symbol, replace, result);
}

EXPORT
void MSHookMessageEx(Class _class, SEL sel, IMP imp, IMP *result) {
	if (class_getInstanceMethod(_class, sel) || class_getClassMethod(_class, sel)) {
		SubHookMessageEx(_class, sel, imp, result);
	} else {
		substitute_panic("libsubstrate-shim: Tried to hook non-existent selector %s on class %s",
			sel_getName(sel), class_getName(_class));
			if (result) *result = NULL;
	}
}

// i don't think anyone uses this function anymore, but it's here for completeness
EXPORT
void MSHookClassPair(Class _class, Class hook, Class old) {
    unsigned int n_methods = 0;
    Method *hooks = class_copyMethodList(hook, &n_methods);
    
    for (unsigned int i = 0; i < n_methods; ++i) {
        SEL selector = method_getName(hooks[i]);
        const char *what = method_getTypeEncoding(hooks[i]);
        
        Method old_mptr = class_getInstanceMethod(old, selector);
        Method cls_mptr = class_getInstanceMethod(_class, selector);
        
        if (cls_mptr) {
            class_addMethod(old, selector, method_getImplementation(hooks[i]), what);
            method_exchangeImplementations(cls_mptr, old_mptr);
        } else {
            class_addMethod(_class, selector, method_getImplementation(hooks[i]), what);
        }
    }
    
    free(hooks);
}

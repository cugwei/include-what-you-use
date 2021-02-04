@import Foo;
#import <Foo/Foo.h>
@import Foo;
#import <stdio.h>
#import "module+i1.h"

#ifndef CLS
# define CLS Test
#endif
@interface CLS : MyClass
@end

/**** iwyu_summary

(tests/objc/module.h has correct #includes/fwd-decls)

***** iwyu_summary */

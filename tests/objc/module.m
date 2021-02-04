//===--- protocol_simple.m - test input file for iwyu ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// IWYU_ARGS: -fmodules -F tests/objc/frameworks -Wno-objc-root-class

#import <Foo/Foo.h>
#import "module.h"
@import Foo;
@import Foo;
#import <stdio.h>
#import <stdio.h>

/* @interface Tsar: Test */
/* - (void)tmp; */
/* @end */

@interface Hui: MyClass
@end


/* #define CLS Test2 */
/* #include "module.h" */

/* @interface Test3 : Test2 */
/* @end */

/* @implementation Test3 */
/* @end */

@implementation Tsar
- (void)tmp {}
@end

void suka(Tsar *t) {}
void padla(Tsar *z) {
  [z tmp];
}
void test() {
  printf("hello");
}

/**** iwyu_summary

(tests/objc/module.m has correct #includes/fwd-decls)

***** iwyu_summary */

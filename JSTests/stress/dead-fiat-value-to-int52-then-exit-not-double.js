function foo() {
    var value = bar($vm.dfgTrue());
    fiatInt52(value);
    fiatInt52(value);
}

var thingy = false;
function bar(p) {
    if (thingy)
        return "hello";
    return p ? 42 : 5.5;
}

noInline(foo);
noInline(bar);

for (var i = 0; i < testLoopCount; ++i)
    foo();

thingy = true;
foo();

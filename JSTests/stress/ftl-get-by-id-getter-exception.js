function foo(o, p) {
    var x = 100;
    var result = 101;
    try {
        x = 102;
        p = 103;
        result = o.f;
        o = 104;
        p = 105;
        x = 106;
    } catch (e) {
        return {outcome: "exception", values: [o, p, x, result]};
    }
    return {outcome: "return", values: [o, p, x, result]};
}

noInline(foo);

// Warm up foo() with polymorphic objects and getters.
for (var i = 0; i < testLoopCount; ++i) {
    var o = {};
    o.__defineGetter__("f", function() {
        return 107;
    });
    if (i & 1)
        o["i" + i] = i; // Make it polymorphic.
    var result = foo(o);
    if (result.outcome !== "return")
        throw "Error in loop: bad outcome: " + result.outcome;
    if (result.values.length !== 4)
        throw "Error in loop: bad number of values: " + result.values.length;
    if (result.values[0] !== 104)
        throw "Error in loop: bad values[0]: " + result.values[0];
    if (result.values[1] !== 105)
        throw "Error in loop: bad values[1]: " + result.values[1];
    if (result.values[2] !== 106)
        throw "Error in loop: bad values[2]: " + result.values[2];
    if (result.values[3] !== 107)
        throw "Error in loop: bad values[3]: " + result.values[3];
}

// Now throw an exception.
var o = {};
o.__defineGetter__("f", function() {
    throw "Error42";
});
var result = foo(o, 108);
if (result.outcome !== "exception")
    throw "Error at end: bad outcome: " + result.outcome;
if (result.values.length !== 4)
    throw "Error at end: bad number of values: " + result.values.length;
if (result.values[0] !== o)
    throw "Error at end: bad values[0]: " + result.values[0];
if (result.values[1] !== 103)
    throw "Error at end: bad values[1]: " + result.values[1];
if (result.values[2] !== 102)
    throw "Error at end: bad values[2]: " + result.values[2];
if (result.values[3] !== 101)
    throw "Error at end: bad values[3]: " + result.values[3];


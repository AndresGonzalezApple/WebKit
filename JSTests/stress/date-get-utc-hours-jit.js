function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error('bad value: ' + actual);
}

function test(date) {
    return date.getUTCHours();
}
noInline(test);

var date = new Date();
var invalid = new Date(NaN);
var expected = date.getUTCHours();
for (var i = 0; i < testLoopCount; ++i) {
    shouldBe(test(date), expected);
    var d = new Date();
    shouldBe(test(d), d.getUTCHours());
    shouldBe(isNaN(test(invalid)), true);
}

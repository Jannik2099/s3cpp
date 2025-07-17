tests = files('enc.cpp', 'iam.cpp', 'iam2.cpp')

fs = import('fs')

foreach testfile : tests
    stem = fs.stem(testfile)
    test(stem, executable(stem, testfile, dependencies: [self_dep]))
endforeach

add_test(NAME regex1 COMMAND ./run-diff.sh regex1.vsrc 10)
add_test(NAME foreach1 COMMAND ./run-diff.sh foreach1.vsrc 100)
add_test(NAME foreach2 COMMAND ./run-diff.sh foreach2.vsrc 10)
add_test(NAME foreach3 COMMAND ./run-diff.sh foreach3.vsrc 10)
add_test(NAME foreach4 COMMAND ./run-diff.sh foreach4.vsrc 1)


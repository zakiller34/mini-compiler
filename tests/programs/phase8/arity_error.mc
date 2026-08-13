// Calling a 1-argument procedure with 2 arguments traps: exit status 255
let f = lambda (a) { a };
f(1, 2)

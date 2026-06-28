# REPL Usage & Interactive Mode

The Sew REPL supports executing expressions interactively or evaluating entire scripts piped from standard input.

## Starting the REPL

To start the interactive REPL in JavaScript mode:
```bash
./build/sew --stdin js
```

This will launch a colorful interactive shell:
```
Sew REPL (js)
Type expressions and press Enter. Press Ctrl+D to exit.

sew> 2 + 2
4
sew> console.log("Hello from Sew!");
Hello from Sew!
```

## Piping Scripts via Stdin

You can also run scripts in a non-interactive, piped mode:
```bash
echo "console.log('Math:', 10 * 10);" | ./build/sew --stdin js
```
Output:
```
Sew v1
ℹ  Eval Mode: js
Math: 100
Eval session took 0 ms
```

## Reflection Bindings

If you provide C++ source headers when launching the REPL, Sew will dynamically parse them, generate the binding glue, compile them into a shared library, and load them into the JS environment.

For example:
```bash
./build/sew include/MyEngine.hpp --stdin js
```
Inside the JS environment, all classes and structs defined in `MyEngine.hpp` are instantiated as globals and can be used directly:
```javascript
sew> const engine = new MyEngine();
sew> engine.start();
```

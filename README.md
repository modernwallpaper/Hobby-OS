## Project Base

This project was build on top of the limine-cxx-template.

## Running

### Debuging

Just parse the DEBUG=1 argument after ```make run```

### Command

i usually run it with this command, but you also have many other options.
Just take a look into the `GNUMakefile`.

```bash
make run-hdd DEBUG=1 QEMUFLAGS="-m 12G -smp 4 -serial stdio" 
```

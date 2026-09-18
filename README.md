# Flash Memory Controller Simulation

### Quick Compilation
To compile run the following command in your terminal:

```bash
g++ -std=c++17 -Wall -Wextra -DSC_DEFAULT_WRITER_POLICY=SC_MANY_WRITERS \
    -I. -I"$SYSTEMC_HOME/include" \
    Bus.cpp Ram.cpp InterruptController.cpp FlashInterface.cpp DmaEngine.cpp CommandProcessor.cpp HostInterface.cpp main.cpp \
    -L"$SYSTEMC_HOME/lib-linux64" -lsystemc \
    -Wl,-rpath,"$SYSTEMC_HOME/lib-linux64" \
    -o main
```

##  Running the Simulation

Once compiled successfully, run the generated binary executable:

```bash
./main
```

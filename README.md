# lurien
*Though my gaze shall no longer fall upon this city, I will act forever in its protection.* - Lurien the Watcher

Lurien is a CPU profiling library written in modern C++ (some C++20 features are used). It is implemented in a single header file so that it is as easy as possible to drop into existing projects. It works by spinning up a thread which periodically checks what code each thread is currently executing and takes samples. When a thread terminates its CPU statistics are accumulated and output in a user-configurable way.

## Usage
The commonly used part of Lurien's interface consists of three macros:

### ```LURIEN_INIT```
Is called to initialise the Lurien library and start the sampling thread. It accepts a unique pointer to a ```lurien::OutputReceiver``` as an argument: unless you want to customise how Lurien's CPU statistics are reported an ```lurien::DefaultOutputReceiver``` should be sufficient here.

### ```LURIEN_SCOPE```
Is used to tell Lurien about a scope whose CPU usage you are interested in learning about. Its argument is the scope name, which should be unique.

### ```LURIEN_STOP```
Tears down Lurien including joining the sampling thread.

## Example Output
Here is representative output from the example.cpp program which spawns 3 threads. We can see the proportion of time each thread spent inside each scope:
```
Thread ID: 0x7fa88e90a700
outer 0.999977
  inner2 0.215728
    inner3 0.0373422
  func2 0.784245
Thread ID: 0x7fa88f90c700
outer 0.999994
  inner2 0.233408
    inner3 0.036108
  func2 0.766579
Thread ID: 0x7fa89010d700
outer 0.999999
  inner2 0.22535
    inner3 0.0360808
  func2 0.774644
```

## Limitations
Lurien does not support profiling the part of a recursive function which makes the recursive call. Re-entering the same scope acts the same way as exiting that scope.

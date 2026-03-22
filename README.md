# chatterino-obs

This is a WIP for embedding Chatterino into OBS.

For build documentation see https://github.com/obsproject/obs-plugintemplate.

Only tested on Windows right now.
Building/testing:

```
git submodule update --init --recursive
cmake --preset windows-x64
cd build_x64
cmake --build . --config RelWithDebInfo && cmake --install . --config RelWithDebInfo
```

On Windows, this includes the SChannel and cert-only TLS backends for Qt.
This will probably cause issues on some systems, because OBS' build doesn't include the OpenSSL backend and the default SChannel backend has caused some issues in the past.

Then you can start OBS. In `Tools`, you should see a new item.

As you can tell, this is not an ideal workflow.

## Windows and clangd

To get compile commands on Windows, you need to open the `build_x64/chatterino-obs.slnx` in Visual Studio and use the Clang Power Tools extension to export compile commands.
In the Solution Explorer, right click on the top solution item and select `Clang Power Tools > Export Compile Commands`.
You may need to tell clangd about the build directory:

```yaml
# .clangd
CompileFlags:
  CompilationDatabase: build_x64
Completion:
  HeaderInsertion: Never
```

# electron-cppgraphql
This is a sample Node Native Module (for Electron) which exposes a [cppgraphqlgen](https://github.com/Microsoft/cppgraphqlgen) service as
a GraphQL endpoint. You could change some of the target configuration to work with regular Node as well, but I used this
with an Electron app hosting [GraphiQL](https://github.com/graphql/graphiql), so that's how it's configured out of the box.

The relevant bits are all in [NodeBinding.cpp](NodeBinding.cpp). I used a locally patched version of the
[vcpkg](https://github.com/Microsoft/vcpkg) package manager to install cppgraphqlgen v1.0.0 and all of its dependencies, the
[PR](https://github.com/Microsoft/vcpkg/pull/5017) to bump that package up to v1.0.0 hasn't been accepted yet as of writing this.
I used [cmake-js](https://github.com/cmake-js/cmake-js) to integrate with vcpkg and CMake (instead of the standard GYP-based process),
and I used the [NAN](https://github.com/nodejs/nan) project to make it somewhat resilient to different versions of Node or Electron.

If you get this working on your own machine, the result should be a file named `./build/Release/electron-cppgraphql.node`,
and if you're on Windows, CMake should put the DLLs exported from cppgraphqlgen next to it. See the cmake-js
README for more information about [how to call the native methods](https://github.com/cmake-js/cmake-js#electron).

### CMake Configuration

I recommend using the [NPM Config](https://github.com/cmake-js/cmake-js#npm-config-integration) support provided by `cmake-js`
to setup any CMake configuration variables you need. For example, I used `vcpkg` to build and install `cppgraphqlgen` and all
of its dependencies, and `vcpkg` depends on setting `CMAKE_TOOLCHAIN_FILE` so it points to
`<vcpkg root>/scripts/buildsystems/vcpkg.cmake`. I used the following command to enable `vcpkg` integration with all of my
`cmake-js` builds (on Windows):

`npm config set cmake_CMAKE_TOOLCHAIN_FILE <vcpkg root>\vcpkg\scripts\buildsystems\vcpkg.cmake`

If you're using `vcpkg` as well, just make sure you replace `<vcpkg root>` with the absolute path to your `vcpkg` installation.
and replace the `\` with `/` on Unix systems.

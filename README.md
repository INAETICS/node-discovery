node-wiring
===========

##Building

1.   Be sure to have installed cmake, apr, apr-util, zlib, curl and jansson libraries
2.   Download, compile and install Celix (sources can be checked out from  https://svn.apache.org/repos/asf/celix/trunk/. Building and configuring instructions are included.)
3.   Create a build folder mkdir build && cd build 
4.   Start cmake with either: cmake -DCELIX_DIR=`<celix installation folder\>` ..  or: ccmake ..  -DCELIX_DIR=`<celix installation folder>` to configure the project via the interactive menu. **Note that you have to specify the celix installation folder as absolute path!**
5.   make all


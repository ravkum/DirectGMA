1) Introduction
Certain use cases require developers to transfer large amounts of data between the GPU
(Graphics Processing Unit) and a device connected to the PCIe interface. AMD GPUs provide
an efficient, low latency mechanism to transfer data between the two devices without requiring
the data to go via system memory. AMD DirectGMA (Direct Graphics Memory Access) is
available as an OpenCL extension named cl_amd_bus_addressable_memory.
This sample demonstrates the throughput achievable by using this extension as compared to
transferring data via system memory. To illustrate the achievable throughput, the sample
implements data transfer between two AMD GPUs that support this extension.

2) Requirements
1. Any system running Microsoft Windows 7 Embedded 64-bit and above with the AMD Catalyst
drivers specified in the Release Notes.
2. AMD Bald Eagle setup with an AMD FirePro W9100 GPU mounted on the PCIe 3.0 x16 slot.
3. Microsoft Visual Studio 2012

3) Compiling and executing the sample

3.1) Setup
By default, the FirePro Catalyst drivers do not enable the DirectGMA extension. To enable the
DirectGMA extension, perform the following steps before running the sample.
1. Open AMD FirePro Catalyst Center.
2. On the Preferences menu, enable Advanced View.
3. On the AMD FireProTM tab to the left, select SDI/DirectGMA
4. Enable SDI/DirectGMA support on both the GPUs by checking the boxes
5. Set the size of the GPU aperture on both GPUs to 96MB
6. Select Apply to apply the changes, and reboot the system.

3.2) Build and Execute
1. Open the oclDirectGMA sample by double-clicking the Visual Studio solution file,
<installDirectory>\samples\oclDirectGMA\build\windows\oclDirectGMAVS12.sln.
2. Build the solution.
3. Run the built executable.


What is this?
-------------

This repo contains the files for a Dekatron bandwidth spinner. It uses an ESP32-C3 to read
out a switch or other network device that talks over SNMP and spins a Dekatron according
to the data flowing over one of its network ports.

Directories in this repo
------------------------

 * case - Contains Fusion360 as well as step and stl files of the case.
 * pcb - Kicad project for the PCB of the spinner
 * firmware - Firmware for the ESP32-C3 in the device. Firmware was compiled using
   ESP-IDF v5.0.4 but you can probably use any v5.x version.

User manual
-----------

After building the PCB, the device needs to be flashed. This can be done by plugging
the USB-C port into a PC while holding the button on the back of the device, then 
flashing as normal. Unplug and replug to start the firmware. After the first succesful
flash, you generally don't need to hold the button when re-flashing.

This device needs to be connected to an USB-PD power supply that can supply 9V or 
more to activate the HV power supply for the Dekatron. If not attached to a 
compatible power supply (e.g. when plugged into a computer), D9 will blink and the 
Dekatron will stay off. The rest of the device will still work, that is, you can
still configure WiFi and SNMP credentials as usual.

On first startup, WiFi needs to be configured. To do this, the device will show
a WiFi access point called 'dekatron'. You can connect to this using a phone or laptop
and it will automatically direct you to a captive webpage where you can select an
access point and enter its password. If the redirect doesn't happen, simply open
a browser and go to http://192.168.4.1/wifi . After you've entered this, the Dekatron
will try to show its best impression of the IP address it got; if you have issues
decyphering this, you can try either connecting to the serial port to see the IP it 
got or checking the DHCP lease table on your router.

The Dekatron will remember the WiFi network and password on subsequent power-ups.
If for some reason you need to change WiFi credentials (e.g. because the original
network is not available anymore, after boot-up simply press and hold the button on
the back of the device for more than 3 seconds, and the 'dekatron' WiFi access point
will show up again.

If you use a webbrowser to connect to the IP, you can configure the SNMP credentials.
Note that you will probably need to change the IP address if your switch, but often the
default community and OIDs will work to use the information of the first network port.
The last decimal of the OID indicates the network port, so increade that to get
another one. Note that some switches don't start counting ports from 1; e.g. my Cisco
starts at 49 instead so the OID for incoming octets on the 2nd network port
I use is .1.3.6.1.2.1.2.2.1.10.50

Here, you can also configure the bandwidth that makes the dekatron spin fastest. You 
can set this to lower than your actual Internet connection can handle; it will simply
spin at its fastests speed for any bandwidth above. Note the bandwidth is in bits 
per second; if you know the bandwidth in bytes per second, simply multiply that by eight.

Finally, you can set a rotation. Some effects (for now only the bit where it shows the
devices IP on the dekatron) assume a a rotation of the physical dekatron. If this is 
wrong, you can adjust it here. Note that the position detection circuit is a bit finnicky,
so depending on your dekatron you may not be able to get this to work stably.


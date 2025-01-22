Overview
********

This folder contains three sample applications that demonstrate the Bluetooth
Low Energy Isochronous Broadcast functionality which has a custome enhancement
that allows for multiple Broadcaster devices to transmit packets in the same
BIG created by a device with a role called 'primary' which has 2 or more BISes.

The primary device will transmit packets in BIS Index 1 and the rest of the BISes
will be left empty to allow for secondary devices to transmit packets instead.

There is another role called 'mixer' which listens to all the BISes and processes
the data.

This caters for the LE Audio use case where a Broadcaster source can transmit audio
on BIS index 1 and secondary devices can transmit on all other BISes (chosen using
a scheme not specified here) and the mixer device can receive all the BISes and
decode and render the audio. This means for example, the audience at a seminar can
listen to the speaker on BIS index 1 and ask questions on the other BISes and 
audio will rendered out of a PA system.

Therefore this folder has three subfolders called primary, secondary and mixer
which implies that at least 3 boards are required.

In these samples the data sent is not audio but known counter and id values that
are logged which can be used to verify that the data is being transmitted and
received correctly.

Requirements
************

* Three boards with Bluetooth Low Energy 5.2 support, e.g Nordic nRF2840
* Firmware is built with the Zephyr Bluetooth Controller.


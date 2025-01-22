.. zephyr:code-sample:: bluetooth_iso_bcast_primary
   :name: Isochronous Broadcast Primary
   :relevant-api: bt_iso bluetooth

   Use the Bluetooth Low Energy Isochronous Broadcaster Primary functionality.

Overview
********

A simple application demonstrating the Bluetooth Low Energy Isochronous
Broadcast Primary functionality where the BIG contains more than 1 BIS and
transmission will only happen in BIS Index 1 and for the rest of  the BISes
there will be no transmissions to allow for secondary devices to transmit
packets instead.

Used with two other sample apps as follow:
     :zephyr_file:`samples/bluetooth/iso_bcast_secondary`
     :zephyr_file:`samples/bluetooth/iso_bcast_mixer`

This proprietary functionality enables the `iso_bcast_secondary` to inject
a BIS defined by a BIG created by `iso_bcast_primary` that normally would be
transmitted by a standard le audio Broadcaster source, which will then be 
picked up by a third device called `iso_bcast_mixer` which  would receive all
BISes defined by the BIG, decode and render it.

Requirements
************

* BlueZ running on the host, or
* A board with Bluetooth Low Energy 5.2 support
* A Bluetooth Controller and board that supports setting
  CONFIG_BT_CTLR_ADV_ISO=y

Building and Running
********************

This sample can be found under :zephyr_file:`samples/bluetooth/iso_bcast_primary` in
the Zephyr tree. Use ``-DEXTRA_CONF_FILE=overlay-bt_ll_sw_split.conf`` to enable
required ISO feature support in Zephyr Bluetooth Controller on supported boards.

Used with the samples found under :zephyr_file:`samples/bluetooth/iso_bcast_secondary`
and :zephyr_file:`samples/bluetooth/iso_bcast_mixer` in the Zephyr tree that will scan,
establish a periodic advertising synchronization, generate BIGInfo reports and 
synchronize to BIG events from this sample.

See :zephyr:code-sample-category:`bluetooth` samples for details.

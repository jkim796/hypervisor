#!/bin/bash

virsh -c qemu:///system setmaxmem vm0 512M --config
virsh -c qemu:///system setmaxmem vm1 512M --config
virsh -c qemu:///system setmaxmem vm2 512M --config
virsh -c qemu:///system setmaxmem vm3 512M --config

virsh -c qemu:///system start vm0
virsh -c qemu:///system start vm1
virsh -c qemu:///system start vm2
virsh -c qemu:///system start vm3

virsh -c qemu:///system list

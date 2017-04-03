#!/bin/bash

virsh -c qemu:///system destroy vm0
virsh -c qemu:///system destroy vm1
virsh -c qemu:///system destroy vm2
virsh -c qemu:///system destroy vm3

virsh -c qemu:///system list

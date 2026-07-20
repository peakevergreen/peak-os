# Peak OS — Vagrant wrapper
#
# Peak OS is a from-scratch kernel with no SSH. This Vagrantfile exists so
# `vagrant up` / `./scripts/vagrant-up.sh` can launch the same ISO QEMU uses.
# Prefer: ./scripts/run-qemu.sh

ISO_PATH = File.expand_path("build/peak-os.iso", __dir__)

Vagrant.configure("2") do |config|
  # Box is unused for Peak OS itself; required by Vagrant's plugin model.
  config.vm.box = "generic/alpine316"
  config.vm.box_check_update = false
  config.vm.synced_folder ".", "/vagrant", disabled: true
  config.ssh.insert_key = false
  config.ssh.username = "vagrant"

  config.trigger.before :up do |trigger|
    trigger.name = "Build Peak OS ISO"
    trigger.run = { inline: "make iso" }
  end

  # Primary path used by scripts/vagrant-up.sh when the plugin works:
  # boot our ISO with Cocoa display. If this fails, the shell script falls
  # back to ./scripts/run-qemu.sh.
  config.vm.provider "qemu" do |qe|
    qe.arch = "x86_64"
    qe.machine = "q35"
    qe.memory = "256"
    qe.smp = "1"
    qe.extra_qemu_args = [
      "-cdrom", ISO_PATH,
      "-boot", "d",
      "-display", "cocoa",
      "-serial", "stdio",
      "-no-reboot"
    ]
  end
end

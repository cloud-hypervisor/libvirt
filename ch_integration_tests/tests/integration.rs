// Copyright © 2021 Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//

#[cfg(test)]
mod tests {
    use std::ffi::OsStr;
    use std::io;
    use std::process::{Child, Command, Stdio};
    use std::thread;

    fn spawn_libvirtd() -> io::Result<Child> {
        Command::new("libvirtd")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
    }

    fn spawn_virsh<I, S>(args: I) -> io::Result<Child>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<OsStr>,
    {
        Command::new("virsh")
            .args(&["-c", "ch:///system"])
            .args(args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
    }

    #[test]
    fn test_uri() {
        let mut libvirtd = spawn_libvirtd().unwrap();
        thread::sleep(std::time::Duration::new(5, 0));

        let output = spawn_virsh(&["uri"]).unwrap().wait_with_output().unwrap();

        libvirtd.kill().unwrap();
        let libvirtd_output = libvirtd.wait_with_output().unwrap();

        eprintln!(
            "libvirtd stdout\n\n{}\n\nlibvirtd stderr\n\n{}",
            std::str::from_utf8(&libvirtd_output.stdout).unwrap(),
            std::str::from_utf8(&libvirtd_output.stderr).unwrap()
        );

        assert_eq!(
            std::str::from_utf8(&output.stdout).unwrap().trim(),
            "ch:///system"
        );
    }
}

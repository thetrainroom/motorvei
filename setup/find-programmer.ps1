# Print the USB device address of the 8401 programmer (CP210x) on each USBPcap root hub, e.g. "1 7".
foreach ($i in 1..4) {
  $out = & 'C:\Program Files\USBPcap\USBPcapCMD.exe' --extcap-interface ("\\.\USBPcap" + $i) --extcap-config 2>$null
  foreach ($line in $out) {
    if ($line -match '\{value=(\d+)\}\{display=\[\d+\] Silicon Labs CP210x') { "$i $($Matches[1])" }
  }
}

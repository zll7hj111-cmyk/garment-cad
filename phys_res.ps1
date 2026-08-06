Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Disp {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)]
  public struct DEVMODE {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
    public short dmSpecVersion; public short dmDriverVersion; public short dmSize;
    public short dmDriverExtra; public int dmFields; public int dmPositionX; public int dmPositionY;
    public int dmDisplayOrientation; public int dmDisplayFixedOutput;
    public short dmColor; public short dmDuplex; public short dmYResolution; public short dmTTOption;
    public short dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
    public short dmLogPixels; public int dmBitsPerPel; public int dmPelsWidth; public int dmPelsHeight;
    public int dmDisplayFlags; public int dmDisplayFrequency;
  }
  [DllImport("user32.dll", CharSet=CharSet.Ansi)]
  public static extern bool EnumDisplaySettings(string deviceName, int modeNum, ref DEVMODE devMode);
  public const int ENUM_CURRENT_SETTINGS = -1;
}
"@
$dm = New-Object Disp+DEVMODE
$dm.dmSize = [int16][System.Runtime.InteropServices.Marshal]::SizeOf($dm)
[void][Disp]::EnumDisplaySettings($null, [Disp]::ENUM_CURRENT_SETTINGS, [ref]$dm)
Write-Output ("PHYSICAL {0}x{1} @ {2}Hz logpixels={3}" -f $dm.dmPelsWidth, $dm.dmPelsHeight, $dm.dmDisplayFrequency, $dm.dmLogPixels)

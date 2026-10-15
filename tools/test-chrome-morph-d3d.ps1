# SPDX-License-Identifier: GPL-2.0-or-later
# Compile the same shader arithmetic with Windows' real HLSL compiler.
# Native OBS parsing and alpha readback are covered by chrome-morph-obs-test.cpp.
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ChromeHlsl {
  [DllImport("d3dcompiler_47.dll", CharSet=CharSet.Ansi)]
  static extern int D3DCompile(byte[] code, UIntPtr size, string name, IntPtr defines,
    IntPtr include, string entry, string profile, uint flags, uint flags2,
    out IntPtr shader, out IntPtr errors);
  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  delegate IntPtr GetPointer(IntPtr self);
  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  delegate UIntPtr GetSize(IntPtr self);
  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  delegate uint Release(IntPtr self);
  public static void Compile(string source) {
    byte[] bytes = System.Text.Encoding.UTF8.GetBytes(source);
    IntPtr shader, errors;
    int hr = D3DCompile(bytes, (UIntPtr)bytes.Length, "chrome-morph.effect", IntPtr.Zero,
      IntPtr.Zero, "PSDefault", "ps_5_0", 1u << 11, 0, out shader, out errors);
    string message = "";
    if (errors != IntPtr.Zero) {
      IntPtr vt = Marshal.ReadIntPtr(errors);
      var ptr = Marshal.GetDelegateForFunctionPointer<GetPointer>(Marshal.ReadIntPtr(vt,3*IntPtr.Size));
      var len = Marshal.GetDelegateForFunctionPointer<GetSize>(Marshal.ReadIntPtr(vt,4*IntPtr.Size));
      message = Marshal.PtrToStringAnsi(ptr(errors), checked((int)len(errors).ToUInt64()));
      Marshal.GetDelegateForFunctionPointer<Release>(Marshal.ReadIntPtr(vt,2*IntPtr.Size))(errors);
    }
    if (shader != IntPtr.Zero) {
      IntPtr vt = Marshal.ReadIntPtr(shader);
      Marshal.GetDelegateForFunctionPointer<Release>(Marshal.ReadIntPtr(vt,2*IntPtr.Size))(shader);
    }
    if (hr < 0) throw new Exception("HLSL compiler failed: " + message);
    Console.WriteLine("PASS: D3DCompile ps_5_0. " + message);
  }
}
'@
$Shader = Get-Content "$PSScriptRoot/../data/effects/chrome-morph.effect" -Raw
$Shader = $Shader.Substring(0,$Shader.IndexOf('technique Draw'))
$Shader = $Shader.Replace(': TARGET', ': SV_Target')
[ChromeHlsl]::Compile($Shader)

open System
open System.IO.MemoryMappedFiles
open System.Runtime.InteropServices
open System.Text
open Microsoft.FSharp.NativeInterop

#nowarn "9"

[<EntryPoint>]
let main argv =

    use f = MemoryMappedFile.CreateNew("testfile", 1L <<< 20)
    use v = f.CreateViewAccessor()
    let ptr = v.SafeMemoryMappedViewHandle.DangerousGetHandle()

    let arr = Encoding.UTF8.GetBytes "Hello From F#"
    Marshal.Copy(arr, 0, ptr, arr.Length) 

    while true do
        printf "content# "
        let line = System.Console.ReadLine()
        let arr = Encoding.UTF8.GetBytes line
        Marshal.Copy(arr, 0, ptr, arr.Length) 
        NativePtr.write (NativePtr.ofNativeInt (ptr + nativeint arr.Length)) 0uy
        
    0

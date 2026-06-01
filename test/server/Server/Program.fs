
open Aardvark.Base
open System
open System.Runtime.InteropServices
open System.Text

#nowarn "9"

[<EntryPoint>]
let main argv =
    if argv.Length < 3 then
        printfn "Usage: Server <name> <length> <data>"
        Environment.Exit 1

    let name = argv.[0]
    let length = int <| argv.[1]
    let data = argv.[2]

    printfn $"[Server] name = '{name}'"
    printfn $"[Server] length = {length}"
    printfn $"[Server] data = '{data}'"

    let dataBytes = Encoding.UTF8.GetBytes data
    if dataBytes.Length > length then
        printfn $"[Server] Data requires {dataBytes.Length} bytes but only {length} were requested"
        Environment.Exit 1

    use f = SharedMemory.Create(name, int64 length)
    let ptr = f.Pointer

    Marshal.Copy(dataBytes, 0, ptr, dataBytes.Length)

    printfn "[Server] Ready"
    Console.ReadLine() |> ignore

    0
open System
open System.IO.MemoryMappedFiles
open System.Runtime.InteropServices
open System.Text
open Microsoft.FSharp.NativeInterop

#nowarn "9"


type ISharedMemory =
    inherit IDisposable
    abstract member Pointer : nativeint
    abstract member Size : int64

module SharedMemory =
    open System.Runtime.InteropServices

    [<System.Diagnostics.CodeAnalysis.SuppressMessage("NameConventions", "*")>]
    module private Windows =
        type private MappingInfo =
            {
                file : MemoryMappedFile
                view : MemoryMappedViewAccessor
                size : int64
                data : nativeint
            }
            interface ISharedMemory with
                member x.Dispose() =
                    x.view.Dispose()
                    x.file.Dispose()
                member x.Pointer = x.data
                member x.Size = x.size

        let create (name : string) (size : int64) =
            let file = MemoryMappedFile.CreateOrOpen(name, size)
            let view = file.CreateViewAccessor()

            {
                file = file
                view = view
                size = size
                data = view.SafeMemoryMappedViewHandle.DangerousGetHandle()
            } :> ISharedMemory

    [<System.Diagnostics.CodeAnalysis.SuppressMessage("NameConventions", "")>]
    module private Posix =


        [<Flags>]
        type Protection =
            | Read = 0x01
            | Write = 0x02
            | Execute = 0x04

            | ReadWrite = 0x03
            | ReadExecute = 0x05
            | ReadWriteExecute = 0x07

        [<StructLayout(LayoutKind.Sequential); StructuredFormatDisplay("{AsString}")>]
        type FileHandle =
            struct
                val mutable public Id : int
                override x.ToString() = sprintf "f%d" x.Id
                member private x.AsString = x.ToString()
                member x.IsValid = x.Id >= 0
            end        

        [<StructLayout(LayoutKind.Sequential); StructuredFormatDisplay("{AsString}")>]
        type Permission =
            struct
                val mutable public Mask : uint32

                member x.Owner
                    with get() = 
                        (x.Mask >>> 6) &&& 7u |> int |> unbox<Protection>
                    and set (v : Protection) =  
                        x.Mask <- (x.Mask &&& 0xFFFFFE3Fu) ||| ((uint32 v &&& 7u) <<< 6)

                member x.Group
                    with get() = 
                        (x.Mask >>> 3) &&& 7u |> int |> unbox<Protection>
                    and set (v : Protection) =  
                        x.Mask <- (x.Mask &&& 0xFFFFFFC7u) ||| ((uint32 v &&& 7u) <<< 3)

                member x.Other
                    with get() = 
                        (x.Mask) &&& 7u |> int |> unbox<Protection>
                    and set (v : Protection) =  
                        x.Mask <- (x.Mask &&& 0xFFFFFFF8u) ||| (uint32 v &&& 7u)


                member private x.AsString = x.ToString()
                override x.ToString() =
                    let u = x.Owner
                    let g = x.Group
                    let o = x.Other

                    let inline str (p : Protection) =
                        (if p.HasFlag Protection.Execute then "x" else "-") +
                        (if p.HasFlag Protection.Write then "w" else "-") +
                        (if p.HasFlag Protection.Read then "r" else "-")

                    str u + str g + str o

                new(u : Protection, g : Protection, o : Protection) =
                    {
                        Mask = ((uint32 u &&& 7u) <<< 6) ||| ((uint32 g &&& 7u) <<< 3) ||| (uint32 o &&& 7u)
                    }

            end


        [<Flags>]        
        type MapFlags =    
            | Shared = 0x0001
            | Private = 0x0002
            | Fixed = 0x0010
            | Rename = 0x0020
            | NoReserve = 0x0040
            | NoExtend = 0x0100
            | HasSemaphore = 0x0200
            | NoCache = 0x0400
            | Jit = 0x0800
            | Anonymous = 0x1000 

        [<Flags>] 
        type SharedMemoryFlags =
            | SharedLock = 0x0010
            | ExclusiveLock = 0x0020
            | Async = 0x0040
            | NoFollow = 0x0100
            | Create = 0x0200
            | Truncate = 0x0400
            | Exclusive = 0x0800
            | NonBlocking = 0x0004
            | Append = 0x0008        

            | ReadOnly = 0x0000
            | WriteOnly = 0x0001
            | ReadWrite = 0x0002


        type ErrorCode =
            | NoPermission = 1
            | NoSuchFileOrDirectory = 2
            | NoSuchProcess = 3
            | InterruptedSystemCall = 4
            | IOError = 5
            | NoSuchDeviceOrAddress = 6
            | ArgListTooLong = 7
            | ExecFormatError = 8

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true, EntryPoint="shm_open")>]
        extern FileHandle shmopen(string name, SharedMemoryFlags oflag, Permission mode)
        
        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern nativeint mmap(nativeint addr, unativeint size, Protection prot, MapFlags flags, FileHandle fd, unativeint offset)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int munmap(nativeint ptr, unativeint size)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true, EntryPoint="shm_unlink")>]
        extern int shmunlink(string name)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int ftruncate(FileHandle fd, unativeint size)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int close(FileHandle fd)
        
        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true, EntryPoint="strerror")>]
        extern nativeint strerrorInternal(int code)

        let inline strerror (code : int) =
            strerrorInternal code |> Marshal.PtrToStringAnsi


        let create (name : string) (size : int64) =
            // open the shared memory (or create if not existing)
            let mapName = "/" + name;
            shmunlink(mapName) |> ignore
            
            let flags = SharedMemoryFlags.Truncate ||| SharedMemoryFlags.Create ||| SharedMemoryFlags.ReadWrite
            let perm = Permission(Protection.ReadWriteExecute, Protection.ReadWriteExecute, Protection.ReadWriteExecute)

            let fd = shmopen(mapName, flags, perm)
            if not fd.IsValid then 
                let err = Marshal.GetLastWin32Error() |> strerror
                failwithf "[SharedMemory] could not open \"%s\" (ERROR: %s)" name err

            // set the size
            if ftruncate(fd, unativeint size) <> 0 then 
                let err = Marshal.GetLastWin32Error() |> strerror
                shmunlink(mapName) |> ignore
                failwithf "[SharedMemory] could resize \"%s\" to %d bytes (ERROR: %s)" name size err

            // map the memory into our memory
            let ptr = mmap(0n, unativeint size, Protection.ReadWrite, MapFlags.Shared, fd, 0un)
            if ptr = -1n then 
                let err = Marshal.GetLastWin32Error() |> strerror
                shmunlink(mapName) |> ignore
                failwithf "[SharedMemory] could not map \"%s\" (ERROR: %s)" name err

            { new ISharedMemory with
                member x.Pointer = ptr
                member x.Size = size
                member x.Dispose() =
                    let err = munmap(ptr, unativeint size)
                    if err <> 0 then
                        let err = Marshal.GetLastWin32Error() |> strerror
                        close(fd) |> ignore
                        shmunlink(mapName) |> ignore
                        failwithf "[SharedMemory] could not unmap \"%s\" (ERROR: %s)" name err

                    if close(fd) <> 0 then
                        let err = Marshal.GetLastWin32Error() |> strerror
                        shmunlink(mapName) |> ignore
                        failwithf "[SharedMemory] could not close \"%s\" (ERROR: %s)" name err

                    let err = shmunlink(mapName)
                    if err <> 0 then
                        let err = Marshal.GetLastWin32Error() |> strerror
                        failwithf "[SharedMemory] could not unlink %s (ERROR: %s)" name err
            }


    let create (name : string) (size : int64) =
        if RuntimeInformation.IsOSPlatform(OSPlatform.Windows) then 
            Windows.create name size
        elif RuntimeInformation.IsOSPlatform(OSPlatform.OSX) then
            Posix.create name size        
        elif RuntimeInformation.IsOSPlatform(OSPlatform.Linux) then
            Posix.create name size
        else
            failwith "[SharedMemory] unknown platform"

[<EntryPoint>]
let main argv =
    use f = SharedMemory.create "testfile" (4L <<< 20)
    let ptr = f.Pointer

    let arr = Encoding.UTF8.GetBytes "Hello From F#"
    Marshal.Copy(arr, 0, ptr, arr.Length) 

    let mutable running = true
    while running do
        printf "content# "
        let line = System.Console.ReadLine()
        if line <> "quit" then
            let arr = Encoding.UTF8.GetBytes line
            Marshal.Copy(arr, 0, ptr, arr.Length) 
            NativePtr.write (NativePtr.ofNativeInt (ptr + nativeint arr.Length)) 0uy
        else
            running <- false
    0

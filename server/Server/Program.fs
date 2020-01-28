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

        let create (size : int64) =
            let name = Guid.NewGuid() |> string
            let file = MemoryMappedFile.CreateNew(name, size)
            let view = file.CreateViewAccessor()

            {
                file = file
                view = view
                size = size
                data = view.SafeMemoryMappedViewHandle.DangerousGetHandle()
            } :> ISharedMemory

    [<System.Diagnostics.CodeAnalysis.SuppressMessage("NameConventions", "*")>]
    module private Linux =

        open System.IO
 
        let PROT_READ = 0x01
        let PROT_WRITE = 0x02
        let PROT_EXEC = 0x04

        let MAP_SHARED = 0x001

        let O_RDONLY = 0x0
        let O_WRONLY = 0x1
        let O_RDWR = 0x2
        let O_CREAT = 0x40             

        let IPC_RMID = 0

        let IPC_CREAT = 0x200

        type ErrorCode =
            | NoPermission = 1
            | NoSuchFileOrDirectory = 2
            | NoSuchProcess = 3
            | InterruptedSystemCall = 4
            | IOError = 5
            | NoSuchDeviceOrAddress = 6
            | ArgListTooLong = 7
            | ExecFormatError = 8

        [<DllImport("librt", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int shm_open(string name, int oflag, int mode)
        
        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern nativeint mmap(nativeint addr, unativeint size, int prot, int flags, int fd, unativeint offset)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int munmap(nativeint ptr, unativeint size)

        [<DllImport("librt", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int shm_unlink(string name)

        [<DllImport("librt", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int ftruncate(int fd, unativeint size)




        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int ftok(string name, int projId)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int shmget(int key, int size, int shmflag)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int shmdt(nativeint shmaddr)        

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern nativeint shmat(int shm, nativeint shmaddr, int shmflag)

        [<DllImport("libc", CharSet = CharSet.Ansi, SetLastError=true)>]
        extern int shmctl(int shmid, int cmd,  nativeint buf)

        let inline check (name : string) v =
            if v <> LanguagePrimitives.GenericZero then
                let err = Marshal.GetLastWin32Error() |> unbox<ErrorCode>
                failwithf "%s failed with %A (%A)" name v err


        let inline checkPos (name : string) v =
            if v < LanguagePrimitives.GenericZero then
                let err = Marshal.GetLastWin32Error() |> unbox<ErrorCode>
                failwithf "%s failed with %A (%A)" name v err

        let perm (f : int) (u : int) (g : int) (o : int) =
            ((f &&& 7) <<< 9) ||| 
            ((u &&& 7) <<< 6) ||| 
            ((g &&& 7) <<< 3) ||| 
            (o % 8)

        let createPosix (name : string) (size : int64) =
            // open the shared memory (or create if not existing)
            let fd = shm_open(name, O_CREAT ||| O_RDWR, perm 0 6 4 4)
            fd |> checkPos "shm_open"

            // set the size
            if ftruncate(fd, unativeint size) <> 0 then 
                let err = Marshal.GetLastWin32Error() |> unbox<ErrorCode>
                shm_unlink(name) |> check "shm_unlink"
                failwithf "could not set file size for %d (%A)" fd err

            // map the memory into our memory
            let ptr = mmap(0n, unativeint size, PROT_WRITE ||| PROT_READ, MAP_SHARED, fd, 0un)
            if ptr = -1n then 
                let err = Marshal.GetLastWin32Error() |> unbox<ErrorCode>
                shm_unlink(name) |> check "shm_unlink"
                failwithf "could not map %d: %A" fd err

            

            { new ISharedMemory with
                member x.Pointer = ptr
                member x.Size = size
                member x.Dispose() =
                    munmap(ptr, unativeint size) |> check "munmap"
                    shm_unlink(name) |> check "munmap"
            }

        let create (name : string) (size : int64) =
            let p = 
                if Directory.Exists "/dev/shm" then Path.Combine("/dev/shm", name + ".shm")
                else Path.Combine(Path.GetTempPath(), name + ".shm")

            File.WriteAllText(p, "")

            let tok = ftok(p, int 'R') //s.SafeFileHandle.DangerousGetHandle() |> int 
            tok |> checkPos "ftok"

            let shm = shmget(tok, int size, perm 1 6 4 4)
            shm |> checkPos "shmget"

            let ptr = shmat(shm, 0n, 0)
            ptr |> checkPos "shmat"

            { new ISharedMemory with
                member x.Pointer = ptr
                member x.Size = size
                member x.Dispose() =
                    shmdt(ptr) |> check "shmdt"
                    shmctl(shm, IPC_RMID, 0n) |> checkPos "shmctl"
                    File.Delete p
            }

    let create (name : string) (size : int64) =
        if RuntimeInformation.IsOSPlatform(OSPlatform.Windows) then 
            Windows.create size
        else 
            Linux.create name size


[<EntryPoint>]
let main argv =
    use f = SharedMemory.create "testfile" (1L <<< 20)
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

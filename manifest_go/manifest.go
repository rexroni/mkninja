package main

import (
    "fmt"
    "io"
    "os"
    "sort"
    "strings"
    "errors"
    "time"
    "io/fs"
    "bytes"
)

func printHelp() {
    fmt.Fprintf(os.Stderr, "usage: manifest [SEP] OUTPUT <filenames\n")
    fmt.Fprintf(os.Stderr, "where SEP may be one of: -0 -cr -lf -crlf -lfcr\n")
    fmt.Fprintf(os.Stderr, "when SEP is not provided, stdin is split on ")
    fmt.Fprintf(os.Stderr, "automatically-detected line endings\n")
}

func detectEndings(text []byte) string {
    const cr byte = 13; // ascii-encoded \r
    const lf byte = 10; // ascii-encoded \n
    var sep []byte
    for _, c := range text {
        if c == cr || c == lf {
            if len(sep) == 0 {
                // first byte
                sep = append(sep, c)
            } else if sep[0] == c {
                // \r\r or \n\n
                return string(sep)
            } else {
                // mixed case, \r\n or \n\r
                sep = append(sep, c)
                return string(sep)
            }
        } else if len(sep) > 0 {
            // single-char case, \r or \n
            return string(sep)
        }
    }
    // we read the whole string and found no line endings
    return "\000"
}

func filterEmpties(s []string) []string {
    i := 0
    for i+1 < len(s) {
        if len(s[i]) == 0 {
            // replace with final element
            s[i] = s[len(s)-1]
            s = s[:len(s)-1]
            continue
        }
        i++
    }
    // check the final element
    if len(s[len(s)-1]) == 0 {
        s = s[:len(s)-1]
    }
    return s
}

func main() {
    // detect flags
    var sep string
    var nomoreflags bool
    var output string
    for _, arg := range os.Args[1:] {
        if nomoreflags {
            if output != "" {
                printHelp()
                os.Exit(1)
            }
            output = arg;
            continue
        }
        switch arg {
        case "-0":
            sep = "\000"
        case "-cr":
            sep = "\r"
        case "-lf":
            sep = "\n"
        case "-crlf":
            sep = "\r\n"
        case "-lfcr":
            sep = "\n\r"
        case "--":
            nomoreflags = true
        default:
            if output != "" {
                printHelp()
                os.Exit(1)
            }
            output = arg
        }
    }

    if output == "" {
        printHelp()
        os.Exit(1)
    }

    byts, err := io.ReadAll(os.Stdin)
    if err != nil {
        fmt.Fprintf(os.Stderr, "error reading from stdin: %v\n", err)
        os.Exit(1)
    }

    if sep == "" {
        sep = detectEndings(byts)
    }

    files := strings.Split(string(byts), sep)
    files = filterEmpties(files)

    // sort the input files
    sort.Strings(files)

    joined := []byte(strings.Join(files, sep))
    joined = append(joined, sep...)

    // load the output file
    byts, err = os.ReadFile(output)
    if err != nil {
        if !errors.Is(err, fs.ErrNotExist) {
            fmt.Fprintf(os.Stderr, "error reading from %v: %v\n", output, err)
            os.Exit(1)
        }
        // output doesn't exist yet, just write it and quit
        err = os.WriteFile(output, joined, 0666)
        if err != nil {
            fmt.Fprintf(os.Stderr, "error writing to %v: %v\n", output, err)
            os.Exit(1)
        }
        return
    }

    if !bytes.Equal(byts, joined) {
        // output has out-of-date contents, overwrite and quit
        err = os.WriteFile(output, joined, 0666)
        if err != nil {
            fmt.Fprintf(os.Stderr, "error writing to %v: %v\n", output, err)
            os.Exit(1)
        }
        return
    }

    // make sure that the output file is newer than any file in the list

    outInfo, err := os.Stat(output)
    if err != nil {
        fmt.Fprintf(os.Stderr, "%v\n", err)
        os.Exit(1)
    }

    for _, file := range files {
        info, err := os.Stat(file)
        if err != nil {
            fmt.Fprintf(os.Stderr, "%v\n", err)
            os.Exit(1)
        }

        if info.ModTime().After(outInfo.ModTime()) {
            // update the output file modified-time and exit
            now := time.Now()
            err = os.Chtimes(output, now, now)
            if err != nil {
                fmt.Fprintf(os.Stderr, "%v\n", err)
                os.Exit(1)
            }
        }
    }
}

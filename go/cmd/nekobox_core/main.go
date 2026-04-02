package main

import (
	"fmt"
	"os"
	_ "unsafe"

	"grpc_server"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/sagernet/sing-box/constant"
)

func main() {
	fmt.Println("sing-box:", constant.Version, "GreenRhythm:", neko_common.Version_neko)
	fmt.Println()

	// greenrhythm_core
	if len(os.Args) > 1 && os.Args[1] == "greenrhythm" {
		neko_common.RunMode = neko_common.RunMode_NekoBox_Core
		grpc_server.RunCore(setupCore, &server{})
		return
	}

	// sing-box standalone mode is no longer supported via boxmain.
	// Use the greenrhythm gRPC mode instead.
	fmt.Println("Usage: greenrhythm_core greenrhythm")
}

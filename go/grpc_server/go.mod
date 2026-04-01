module grpc_server

go 1.24.0

require (
	github.com/grpc-ecosystem/go-grpc-middleware v1.3.0
	github.com/matsuridayo/libneko v1.0.0 // replaced
	google.golang.org/grpc v1.79.1
	google.golang.org/protobuf v1.36.11
)

require (
	golang.org/x/net v0.48.0 // indirect
	golang.org/x/sys v0.39.0 // indirect
	golang.org/x/text v0.32.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20251202230838-ff82c1b0f217 // indirect
)

exclude cloud.google.com/go v0.26.0

replace github.com/matsuridayo/libneko v1.0.0 => ../../../libneko

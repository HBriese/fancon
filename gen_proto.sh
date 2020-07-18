#!/bin/sh

SRC=proto
FILE=$SRC/DevicesSpec.proto
CPP=src/proto
JS=../fanconw/src/proto

DIR=$(dirname "$0")
cd "$DIR"

# fancon
mkdir -p ./$CPP
rm -f ./$CPP/*

protoc -I=./$SRC ./$FILE \
  --cpp_out=./$CPP \
  --grpc_out=./$CPP \
  --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin)

# Silence compiler warnings from protoc generated code
CLANG_DIAG_S=$(
  cat <<-END
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif // __clang__
END
)
CLANG_DIAG_E=$(
  cat <<-END
#if defined(__clang__)
#pragma clang diagnostic pop  // -Wunused-parameter
#elif defined(__GNUC__)
#pragma GCC diagnostic pop    // -Wunused-but-set-variable
#endif // __clang__
END
)
for filename in ./"$CPP"/*.h; do
  printf "%s\n\n%s\n\n%s\n" "$CLANG_DIAG_S" "$(cat "./$filename")" "$CLANG_DIAG_E" >"./$filename"
done

# fanconw
#mkdir -p ./$JS
#rm -f ./$JS/*

#GRPC_WEB=$(which protoc-gen-grpc-web)
#if [ $? -ne 0 ]; then
#    GRPC_WEB=./node_modules/protoc-gen-grpc-web/bin/protoc-gen-grpc-web
#    if [ ! -f $GRPC_WEB ]; then
#        npm install grpc-tools protoc-gen-grpc-web
#    fi
#fi

#protoc -I=./$SRC ./$FILE \
#    --js_out=import_style=commonjs:$JS \
#    --grpc-web_out=import_style=commonjs,mode=grpcwebtext:$JS \
#    --plugin=protoc-gen-grpc-web=$GRPC_WEB

#if [ $? -eq 0 ]; then
#    ## Prepend /* eslint-disable */ to ignore compiler complaints
#    for filename in "$JS/*.js"; do
#	    echo "/* eslint-disable */\n$(cat ./$filename)" > ./$filename
#    done
#else
#    echo 'protoc-grpc-web FAILED'
#    ls $JS
#fi

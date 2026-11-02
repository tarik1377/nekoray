package main

import (
	"errors"
	"path/filepath"
	"strings"
)

type launchOptions struct {
	install, data string
	portable      bool
	action        []string
}

func parseLaunchOptions(executable string, arguments []string) (launchOptions, error) {
	options := launchOptions{install: filepath.Dir(executable)}
	seen := map[string]bool{}
	for index := 0; index < len(arguments); index++ {
		flag := arguments[index]
		switch flag {
		case "--install-dir", "--data-dir":
			if seen[flag] || index+1 >= len(arguments) {
				return options, errors.New("неверные параметры каталога установки/данных")
			}
			seen[flag] = true
			index++
			value := arguments[index]
			if !filepath.IsAbs(value) || strings.IndexByte(value, 0) >= 0 {
				return options, errors.New("каталог установки/данных должен быть абсолютным путём")
			}
			if flag == "--install-dir" {
				options.install = filepath.Clean(value)
			} else {
				options.data = filepath.Clean(value)
			}
		case "--portable":
			if seen[flag] {
				return options, errors.New("повторный параметр portable")
			}
			seen[flag], options.portable = true, true
		case "--rollback":
			options.action = append(options.action, flag)
		default:
			if strings.HasPrefix(flag, "--") {
				return options, errors.New("неизвестный параметр установщика")
			}
			options.action = append(options.action, flag)
		}
	}
	if len(options.action) > 1 || options.portable && options.data != "" {
		return options, errors.New("несовместимые параметры установщика")
	}
	return options, nil
}

func (options launchOptions) forwarded() []string {
	arguments := []string{"--install-dir", options.install}
	if options.data != "" {
		arguments = append(arguments, "--data-dir", options.data)
	}
	if options.portable {
		arguments = append(arguments, "--portable")
	}
	return append(arguments, options.action...)
}

func (options launchOptions) applicationArgs() []string {
	if options.data != "" {
		return []string{"-appdata", options.data}
	}
	if options.portable {
		return []string{"-portable"}
	}
	return nil
}

// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

//go:build tools

// Package tools declares CLI tool dependencies so `go mod tidy` does not prune
// them and Dependabot can track version updates via go.mod.
package tools

import _ "github.com/google/addlicense"

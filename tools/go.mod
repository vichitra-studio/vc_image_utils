// This module exists solely to declare tool dependencies so Dependabot can
// track and propose version updates. The version here MUST stay in sync with:
//   .pre-commit-config.yaml  (additional_dependencies)
//   .github/workflows/license-check.yml  (go install)
//
// After Dependabot bumps the version here, update those two files to match,
// then run `go mod tidy` in this directory to regenerate go.sum.

module github.com/vichitra-studio/vc_image_utils/tools

go 1.21

require github.com/google/addlicense v1.2.0

require (
	github.com/bmatcuk/doublestar/v4 v4.0.2 // indirect
	golang.org/x/sync v0.0.0-20190911185100-cd5d95a43a6e // indirect
)

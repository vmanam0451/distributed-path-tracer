.PHONY: dev build


dev:
	cd path-tracer-backend && go run . & \
	cd path-tracer-frontend && npm run dev & \
	wait

# Build frontend then start Go (production)
build:
	cd path-tracer-frontend && npm run build
	cd path-tracer-backend && go build -o server .

start: build
	./path-tracer-backend/server

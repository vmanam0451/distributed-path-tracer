.PHONY: dev build


dev:
	@trap 'kill %1 %2 2>/dev/null; exit 0' INT; \
	cd path-tracer-backend && go run . & \
	cd path-tracer-frontend && npm run dev & \
	wait

# Build frontend then start Go (production)
build:
	cd path-tracer-frontend && npm run build
	cd path-tracer-backend && go build -o server .

start: build
	./path-tracer-backend/server

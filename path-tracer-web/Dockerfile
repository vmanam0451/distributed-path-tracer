FROM --platform=linux/amd64 node:22-alpine AS frontend
WORKDIR /app/frontend
COPY path-tracer-frontend/package*.json ./
RUN npm ci
COPY path-tracer-frontend/ ./
RUN npm run build


FROM --platform=linux/amd64 golang:1.25-alpine AS backend
WORKDIR /app/backend
COPY path-tracer-backend/go.mod path-tracer-backend/go.sum ./
RUN go mod download
COPY path-tracer-backend/ ./
RUN go mod tidy && CGO_ENABLED=0 GOOS=linux go build -o server .

# Stage 3: Final image
FROM --platform=linux/amd64 alpine:3.21
WORKDIR /app
COPY --from=backend /app/backend/server ./server
COPY --from=frontend /app/frontend/dist ./dist

EXPOSE 8080
CMD ["./server"]

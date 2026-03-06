package main

import (
	"net/http"
	"os"

	"github.com/gin-gonic/gin"
)

func main() {
	router := gin.Default()

	// API routes
	api := router.Group("/api")
	{
		api.GET("/ping", func(c *gin.Context) {
			c.JSON(http.StatusOK, gin.H{
				"message": "pong",
			})
		})
	}

	// Serve frontend static files in production
	distDir := "../path-tracer-frontend/dist"
	if _, err := os.Stat(distDir); err == nil {
		router.Static("/assets", distDir+"/assets")
		router.StaticFile("/", distDir+"/index.html")
		router.NoRoute(func(c *gin.Context) {
			c.File(distDir + "/index.html")
		})
	}

	router.Run() // listens on 0.0.0.0:8080 by default
}

package main

import (
	"net/http"
	"os"
	"pathtracerbackend/handlers"
	"github.com/gin-gonic/gin"
)

func main() {
	router := gin.Default()

	api := router.Group("/api")
	{
		api.GET("/ping", func(c *gin.Context) {
			c.JSON(http.StatusOK, gin.H{
				"message": "pong",
			})
		})

		api.POST("/render", handlers.Render)
	}

	distDir := "./dist"
	if _, err := os.Stat(distDir); err == nil {
		router.Static("/assets", distDir+"/assets")
		router.StaticFile("/", distDir+"/index.html")
		router.NoRoute(func(c *gin.Context) {
			c.File(distDir + "/index.html")
		})
	}

	router.Run() // listens on 0.0.0.0:8080 by default
}

SELECT DestCityName, COUNT( DISTINCT OriginCityName) FROM ontime WHERE Year BETWEEN 1999 and 2002 GROUP BY DestCityName ORDER BY 2 DESC LIMIT 10;
